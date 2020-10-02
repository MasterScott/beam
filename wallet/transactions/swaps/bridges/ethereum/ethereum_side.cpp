// Copyright 2020 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "ethereum_side.h"

#include "common.h"
#include "utility/logger.h"

using namespace ECC;

namespace
{
    // TODO: check
    constexpr uint32_t kExternalHeightMaxDifference = 50;

    const std::string kLockMethodHash = "0xae052147";
    const std::string kRefundMethodHash = "0x7249fbb6";
    const std::string kRedeemMethodHash = "0xb31597ad";
    const std::string kGetDetailsMethodHash = "0x6bfec360";

    // TODO: -> settings
    const libbitcoin::short_hash kContractAddress = beam::ethereum::ConvertStrToEthAddress("0xBcb29073ebFf87eFD2a9800BF51a89ad89b3070E");
}

namespace beam::wallet
{
EthereumSide::EthereumSide(BaseTransaction& tx, ethereum::IBridge::Ptr ethBridge, ethereum::ISettingsProvider& settingsProvider, bool isBeamSide)
    : m_tx(tx),
      m_ethBridge(ethBridge),
      m_settingsProvider(settingsProvider),
      m_isEthOwner(!isBeamSide)
{
}

EthereumSide::~EthereumSide()
{}

bool EthereumSide::Initialize()
{
    if (!m_blockCount)
    {
        GetBlockCount(true);
        return false;
    }

    if (m_isEthOwner)
    {
        InitSecret();
    }
    // InitLocalKeys - ? init publicSwap & secretSwap keys
    return false;
}

bool EthereumSide::InitLockTime()
{
    auto height = m_blockCount;
    assert(height);

    LOG_DEBUG() << "InitLockTime height = " << height;

    auto externalLockPeriod = height + GetLockTimeInBlocks();
    m_tx.SetParameter(TxParameterID::AtomicSwapExternalLockTime, externalLockPeriod);

    return true;
}

bool EthereumSide::ValidateLockTime()
{
    auto height = m_blockCount;
    assert(height);
    auto externalLockTime = m_tx.GetMandatoryParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);

    LOG_DEBUG() << "ValidateLockTime height = " << height << " external = " << externalLockTime;

    if (externalLockTime <= height)
    {
        return false;
    }

    double blocksPerBeamBlock = GetBlocksPerHour() / beam::Rules::get().DA.Target_s;
    Height beamCurrentHeight = m_tx.GetWalletDB()->getCurrentHeight();
    Height beamHeightDiff = beamCurrentHeight - m_tx.GetMandatoryParameter<Height>(TxParameterID::MinHeight, SubTxIndex::BEAM_LOCK_TX);

    Height peerMinHeight = externalLockTime - GetLockTimeInBlocks();
    Height peerEstCurrentHeight = peerMinHeight + static_cast<Height>(std::ceil(blocksPerBeamBlock * beamHeightDiff));

    return peerEstCurrentHeight >= height - kExternalHeightMaxDifference
        && peerEstCurrentHeight <= height + kExternalHeightMaxDifference;
}

void EthereumSide::AddTxDetails(SetTxParameter& /*txParameters*/)
{
    // addd LOCK_TX txID ?
}

bool EthereumSide::ConfirmLockTx()
{
    if (m_isEthOwner)
    {
        return true;
    }
    
    GetBlockCount();

    if (m_SwapLockTxConfirmations < GetTxMinConfirmations())
    {
        auto secretHash = GetSecretHash();

        // else: "contract call" + getTransactionReceipt (mb only "contract call")
        m_ethBridge->call(kContractAddress,
                          kGetDetailsMethodHash + libbitcoin::encode_base16(secretHash),
                          [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, const nlohmann::json& result)
        {
            if (weak.expired())
            {
                return;
            }

            if (error.m_type != ethereum::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            // TODO: parse result! check "value" and save m_SwapLockTxConfirmations
            
            uintBig swapAmount = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapAmount);

            std::string resultStr = result.get<std::string>();
            auto resultData = beam::from_hex(std::string(resultStr.begin() + 2, resultStr.end()));
            ECC::uintBig amount = Zero;
            std::move(resultData.begin() + 32, resultData.end(), std::begin(amount.m_pData));

            if (amount != swapAmount)
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]"
                    << " Unexpected amount, expected: " << swapAmount << ", got: " << amount;
                m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidAmount, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return;
            }

            // TODO
            ECC::uintBig blockNumber = Zero;
            std::move(resultData.begin(), resultData.begin() + 32, std::begin(blockNumber.m_pData));
        });
        return false;
    }

    return true;
}

bool EthereumSide::ConfirmRefundTx()
{
    return ConfirmWithdrawTx(SubTxIndex::REFUND_TX);
}

bool EthereumSide::ConfirmRedeemTx()
{
    return ConfirmWithdrawTx(SubTxIndex::REDEEM_TX);
}

bool EthereumSide::ConfirmWithdrawTx(SubTxID subTxID)
{
    std::string txID;
    if (!m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, subTxID))
        return false;

    if (m_WithdrawTxConfirmations < GetTxMinConfirmations())
    {
        GetWithdrawTxConfirmations(subTxID);
        return false;
    }

    return true;
}

void EthereumSide::GetWithdrawTxConfirmations(SubTxID subTxID)
{
    GetBlockCount();

    // getTransactionReceipt
    std::string txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, subTxID);

    m_ethBridge->getTransactionReceipt(txID, [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, const nlohmann::json& result)
    {
        if (weak.expired())
        {
            return;
        }

        if (error.m_type != ethereum::IBridge::None)
        {
            m_tx.UpdateOnNextTip();
            return;
        }

        // TODO: parse result!
        assert(std::stoull(result["status"].get<std::string>(), nullptr, 16) == 1);

        uint64_t txBlockNumber = std::stoull(result["blockNumber"].get<std::string>(), nullptr, 16);
        auto currentBlockCount = GetBlockCount();
        if (currentBlockCount >= txBlockNumber)
        {
            m_WithdrawTxConfirmations = currentBlockCount - txBlockNumber;
        }
    }
    );
}

bool EthereumSide::SendLockTx()
{
    auto secretHash = GetSecretHash();
    auto participantStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);
    auto participant = ethereum::ConvertStrToEthAddress(participantStr);
    uintBig refundTimeInBlocks = GetLockTimeInBlocks();

    // LockMethodHash + refundTimeInBlocks + hashedSecret + participant
    libbitcoin::data_chunk data;
    data.reserve(4 + 32 + 32 + 32);
    libbitcoin::decode_base16(data, std::string(std::begin(kLockMethodHash) + 2, std::end(kLockMethodHash)));
    data.insert(data.end(), std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData));
    data.insert(data.end(), std::begin(secretHash), std::end(secretHash));
    // address's size is 20, so fill 12 elements by 0x00
    data.insert(data.end(), 12u, 0x00);
    data.insert(data.end(), std::begin(participant), std::end(participant));

    uintBig swapAmount = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapAmount);
    m_ethBridge->send(kContractAddress, data, swapAmount, GetGas(), GetGasPrice(),
        [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
    {
        if (!weak.expired())
        {
            if (error.m_type != ethereum::IBridge::None)
            {
                // TODO: handle error
                return;
            }

            m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    });
    return true;
}

bool EthereumSide::SendRefund()
{
    // TODO: check
    std::string txID;
    if (m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::REFUND_TX))
        return true;

    auto secretHash = GetSecretHash();

    // kRefundMethodHash + secretHash
    libbitcoin::data_chunk data;
    data.reserve(4 + 32 + 32);
    libbitcoin::decode_base16(data, std::string(std::begin(kRefundMethodHash) + 2, std::end(kRefundMethodHash)));
    data.insert(data.end(), std::begin(secretHash), std::end(secretHash));

    uintBig swapAmount = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapAmount);

    m_ethBridge->send(kContractAddress, data, swapAmount, GetGas(), GetGasPrice(),
        [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
    {
        if (!weak.expired())
        {
            OnSentWithdrawTx(SubTxIndex::BEAM_REFUND_TX, error, txHash);
        }
    });
    return true;
}

bool EthereumSide::SendRedeem()
{
    // TODO: check
    std::string txID;
    if (m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::REDEEM_TX))
        return true;

    auto secretHash = GetSecretHash();
    auto secret = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapSecretPrivateKey, SubTxIndex::BEAM_REDEEM_TX);

    // kRedeemMethodHash + secret + secretHash
    libbitcoin::data_chunk data;
    data.reserve(4 + 32 + 32);
    libbitcoin::decode_base16(data, std::string(std::begin(kRedeemMethodHash) + 2, std::end(kRedeemMethodHash)));
    data.insert(data.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
    data.insert(data.end(), std::begin(secretHash), std::end(secretHash));

    uintBig swapAmount = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapAmount);
    m_ethBridge->send(kContractAddress, data, swapAmount, GetGas(), GetGasPrice(),
        [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
    {
        if (!weak.expired())
        {
            OnSentWithdrawTx(SubTxIndex::BEAM_REDEEM_TX, error, txHash);
        }
    });

    return true;
}

void EthereumSide::OnSentWithdrawTx(SubTxID subTxID, const ethereum::IBridge::Error& error, const std::string& txHash)
{
    if (error.m_type != ethereum::IBridge::None)
    {
        // TODO: handle error
        return;
    }

    m_tx.SetParameter(TxParameterID::Confirmations, uint32_t(0), false, subTxID);
    m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, false, subTxID);
    m_tx.UpdateAsync();
}

bool EthereumSide::IsLockTimeExpired()
{
    uint64_t height = GetBlockCount();
    uint64_t lockHeight = 0;
    m_tx.GetParameter(TxParameterID::AtomicSwapExternalLockTime, lockHeight);

    return height >= lockHeight;
}

bool EthereumSide::HasEnoughTimeToProcessLockTx()
{
    Height lockTxMaxHeight = MaxHeight;
    if (m_tx.GetParameter(TxParameterID::MaxHeight, lockTxMaxHeight, SubTxIndex::BEAM_LOCK_TX))
    {
        Block::SystemState::Full systemState;
        if (m_tx.GetTip(systemState) && systemState.m_Height > lockTxMaxHeight - GetLockTxEstimatedTimeInBeamBlocks())
        {
            return false;
        }
    }
    return true;
}

bool EthereumSide::IsQuickRefundAvailable()
{
    return false;
}

uint64_t EthereumSide::GetBlockCount(bool notify)
{
    m_ethBridge->getBlockNumber([this, weak = this->weak_from_this(), notify](const ethereum::IBridge::Error& error, uint64_t blockCount)
    {
        if (!weak.expired())
        {
            if (error.m_type != ethereum::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            if (blockCount != m_blockCount)
            {
                m_blockCount = blockCount;
                m_tx.SetParameter(TxParameterID::AtomicSwapExternalHeight, m_blockCount, true);

                if (notify)
                {
                    m_tx.UpdateAsync();
                }
            }
        }
    });
    return m_blockCount;
}

void EthereumSide::InitSecret()
{
    NoLeak<uintBig> secretPrivateKey;
    GenRandom(secretPrivateKey.V);
    m_tx.SetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, false, BEAM_REDEEM_TX);
}

uint16_t EthereumSide::GetTxMinConfirmations() const
{
    return m_settingsProvider.GetSettings().GetTxMinConfirmations();
}

uint32_t EthereumSide::GetLockTimeInBlocks() const
{
    return m_settingsProvider.GetSettings().GetLockTimeInBlocks();
}

double EthereumSide::GetBlocksPerHour() const
{
    return m_settingsProvider.GetSettings().GetBlocksPerHour();
}

uint32_t EthereumSide::GetLockTxEstimatedTimeInBeamBlocks() const
{
    // TODO: check
    return 10;
}

ByteBuffer EthereumSide::GetSecretHash() const
{
    Point publicKeyPoint = m_tx.GetMandatoryParameter<Point>(TxParameterID::AtomicSwapSecretPublicKey, SubTxIndex::BEAM_REDEEM_TX);
    return SerializePubkey(ConvertPointToPubkey(publicKeyPoint));
}

ECC::uintBig EthereumSide::GetGas() const
{
    // TODO: -> settings
    return 200000u;
}

ECC::uintBig EthereumSide::GetGasPrice() const
{
    // TODO: -> settings
    return 3000000u;
}

} // namespace beam::wallet