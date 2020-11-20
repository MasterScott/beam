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

#include <string>
#include <vector>

#include "wallet/transactions/swaps/common.h"

namespace beam::ethereum
{
struct Settings
{
    std::string m_address = "";
    std::vector<std::string> m_secretWords = {};
    uint32_t m_accountIndex = 0;
    bool m_shouldConnect = false;
    uint16_t m_txMinConfirmations = 12;
    uint32_t m_lockTimeInBlocks = 12 * 60 * 4;  // 12h
    double m_blocksPerHour = 250;
    // TODO roman.strilets need to investigate
    Amount m_minFeeRate = 1;
    std::string m_swapHashlockContractAddress = "";
    std::string m_swapContractAddress = "";
    uint64_t m_lockTxGasLimit = 200'000;
    uint64_t m_withdrawTxGasLimit = 200'000;

    std::string m_erc20SwapHashlockContractAddress;
    std::string m_erc20SwapContractAddress;

    // TODO: change
    std::string m_daiContractAddress;
    std::string m_usdtContractAddress;
    std::string m_wbtcContractAddress;

    bool IsInitialized() const;
    bool IsTokenInitialized(beam::wallet::AtomicSwapCoin swapCoin) const;
    bool IsActivated() const;

    uint16_t GetTxMinConfirmations() const;
    uint32_t GetLockTimeInBlocks() const;
    double GetBlocksPerHour() const;
    Amount GetMinFeeRate() const;
    std::string GetContractAddress(bool isHashLockScheme = false) const;
    std::string GetERC20SwapContractAddress(bool isHashLockScheme = false) const;

    std::string GetTokenContractAddress(beam::wallet::AtomicSwapCoin swapCoin) const;

    bool operator == (const Settings& other) const
    {
        return m_address == other.m_address &&
            m_secretWords == other.m_secretWords &&
            m_accountIndex == other.m_accountIndex &&
            m_shouldConnect == other.m_shouldConnect;
    }

    bool operator != (const Settings& other) const
    {
        return !(*this == other);
    }
};
} // namespace beam::ethereum