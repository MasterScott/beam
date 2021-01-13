#include "../common.h"
#include "contract.h"
#include "../BeamHeader.h"

export void Ctor(const Pipe::Create& r)
{
	Pipe::StateOut so;
	Utils::ZeroObject(so);
	Utils::Copy(so.m_Cfg, r.m_Cfg.m_Out);
	
	Pipe::StateOut::Key ko;
	Env::SaveVar_T(ko, so);

	Pipe::StateIn si;
	Utils::ZeroObject(si);
	Utils::Copy(si.m_Cfg, r.m_Cfg.m_In);

	Pipe::StateIn::Key ki;
	Env::SaveVar_T(ki, si);
}

export void Dtor(void*)
{
	Env::Halt(); // not supported
}

export void Method_2(const Pipe::SetRemote& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::Halt_if(!Utils::IsZero(si.m_cidRemote) || Utils::IsZero(r.m_cid));

	Utils::Copy(si.m_cidRemote, r.m_cid);
	Env::SaveVar_T(ki, si);

}

void get_MsgHash(HashValue& res, const Pipe::MsgHdr* pMsg, uint32_t nMsg)
{
	HashProcessor hp;
	hp.m_p = Env::HashCreateSha256();
	hp << "b.msg";
	hp.Write(pMsg, nMsg);
	hp >> res;
}

void UpdateState(HashValue& res, const HashValue& hvMsg)
{
	HashProcessor hp;
	hp.m_p = Env::HashCreateSha256();
	hp
		<< "b.pipe"
		<< res
		<< hvMsg
		>> res;
}

void UpdateState(HashValue& res, const Pipe::MsgHdr* pMsg, uint32_t nMsg)
{
	HashValue hvMsg;
	get_MsgHash(hvMsg, pMsg, nMsg);
	UpdateState(res, hvMsg);
}

export void Method_3(const Pipe::PushLocal0& r)
{
	Height h = Env::get_Height();

	uint32_t nSize = r.m_MsgSize + sizeof(Pipe::MsgHdr);
	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);

	if (Env::get_CallDepth() > 1)
		Env::get_CallerCid(1, pMsg->m_Sender);
	else
		Utils::ZeroObject(pMsg->m_Sender);

	Utils::Copy(pMsg->m_Receiver, r.m_Receiver);
	pMsg->m_Height = h;
	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	Pipe::StateOut so;
	Pipe::StateOut::Key ko;
	Env::LoadVar_T(ko, so);

	bool bNewCheckpoint = false;
	if (!so.m_Checkpoint.m_iMsg)
		bNewCheckpoint = true; // the very 1st message
	else
	{
		if ((so.m_Checkpoint.m_iMsg == so.m_Cfg.m_CheckpointMaxMsgs) || (h - so.m_Checkpoint.m_h0 >= so.m_Cfg.m_CheckpointMaxDH))
		{
			bNewCheckpoint = true;
			so.m_Checkpoint.m_iIdx++;
			so.m_Checkpoint.m_iMsg = 0;
		}
	}

	Pipe::OutCheckpoint::Key cpk;
	cpk.m_iCheckpoint_BE = Utils::FromBE(so.m_Checkpoint.m_iIdx);

	HashValue hv;
	if (bNewCheckpoint)
	{
		so.m_Checkpoint.m_h0 = h;
		Utils::ZeroObject(hv);
	}
	else
		Env::LoadVar_T(cpk, hv);

	Pipe::MsgHdr::Key km;
	km.m_iCheckpoint_BE = cpk.m_iCheckpoint_BE;
	km.m_iMsg_BE = Utils::FromBE(so.m_Checkpoint.m_iMsg);
	Env::SaveVar(&km, sizeof(km), pMsg, nSize);

	UpdateState(hv, pMsg, nSize);
	Env::SaveVar_T(cpk, hv);

	so.m_Checkpoint.m_iMsg++;
	Env::SaveVar_T(ko, so);
}

struct MyParser
{
	const uint8_t* m_pPtr;
	MyParser(const void* p) :m_pPtr((const uint8_t*) p) {}

	const uint8_t* Read(uint32_t n)
	{
		auto pVal = m_pPtr;
		m_pPtr += n;
		return pVal;
	}

	template <typename T>
	const T& Read_As()
	{
		return *reinterpret_cast<const T*>(Read(sizeof(T)));
	}
};

void OnUserHdr(const PubKey& pk, const BlockHeader::Full& hdr, const Pipe::StateIn& si)
{
	Env::Halt_if(!hdr.m_Height); // for more safety

	if (!si.m_Cfg.m_FakePoW)
		Env::Halt_if(!hdr.IsValid(&si.m_Cfg.m_RulesRemote));

	Pipe::UserHdr uh;
	hdr.get_Hash(uh.m_hv, &si.m_Cfg.m_RulesRemote);
	Utils::Copy(uh.m_ChainWork, hdr.m_ChainWork);

	Pipe::UserHdr::Key uhk;
	Utils::Copy(uhk.m_Pk, pk);
	uhk.m_Height = hdr.m_Height;

	Env::SaveVar_T(uhk, uh);
}

export void Method_4(const Pipe::PushRemote0& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::Halt_if(!Utils::Cmp(r.m_User, si.m_Dispute.m_Winner)); // already winning

	Pipe::UserInfo ui;
	Pipe::UserInfo::Key kui;
	Utils::Copy(kui.m_Pk, r.m_User);

	bool bNew = true;
	if (!Env::LoadVar_T(kui, ui))
		Utils::ZeroObject(ui);
	else
	{
		bNew = (ui.m_Dispute.m_iIdx != si.m_Dispute.m_iIdx);
		if (bNew)
			Utils::ZeroObject(ui.m_Dispute);
	}

	Env::Halt_if(bNew == !(Pipe::PushRemote0::Flags::Msgs & r.m_Flags)); // new contender - must bring messages

	MyParser p(&r + 1);

	const HashValue* pMsgs = nullptr;
	uint32_t nMsgs = 0;

	kui.m_Type = Pipe::KeyType::UserMsgs;

	if (bNew)
	{
		nMsgs = p.Read_As<uint32_t>();
		Env::Halt_if(!nMsgs);

		uint32_t nSize = sizeof(HashValue) * nMsgs;
		pMsgs = (const HashValue*) p.Read(nSize);

		Env::SaveVar(&kui, sizeof(kui), pMsgs, nSize);
	}

	bool bHdr0 = !!(Pipe::PushRemote0::Flags::Hdr0 & r.m_Flags);
	Env::Halt_if(bHdr0 != (si.m_Dispute.m_Stake && !ui.m_Dispute.m_hMin)); // dispute started (something at stake already) - must bring hdr0, unless brought

	if (bHdr0)
	{
		const auto& hdr = p.Read_As<BlockHeader::Full>();
		OnUserHdr(r.m_User, hdr, si);

		ui.m_Dispute.m_hMin = hdr.m_Height;
		ui.m_Dispute.m_hMax = hdr.m_Height;
		BeamDifficulty::Unpack(ui.m_Dispute.m_Work, hdr.m_PoW.m_Difficulty);

		if (!bNew)
		{
			// load messages, that were saved previously
			nMsgs = Env::LoadVar(&kui, sizeof(kui), nullptr, 0);

			pMsgs = (const HashValue*) Env::StackAlloc(nMsgs);
			nMsgs /= sizeof(HashValue);

			Env::LoadVar(&kui, sizeof(kui), (void*) pMsgs, nMsgs);
		}

		HashValue hv;
		Utils::ZeroObject(hv);

		for (uint32_t i = 0; i < nMsgs; i++)
			UpdateState(hv, pMsgs[i]);

		Pipe::OutCheckpoint::Key cpk;
		cpk.m_iCheckpoint_BE = Utils::FromBE(si.m_Dispute.m_iIdx);

		Merkle::get_ContractVarHash(hv, si.m_cidRemote, KeyTag::Internal, &cpk, sizeof(cpk), &hv, sizeof(hv));

		auto n = p.Read_As<uint32_t>();
		while (n--)
			Merkle::Interpret(hv, p.Read_As<Merkle::Node>());

		Env::Halt_if(Utils::Cmp(hv, hdr.m_Definition));
	}

	if (Pipe::PushRemote0::Flags::HdrsUp & r.m_Flags)
	{
		Env::Halt_if(!si.m_Dispute.m_Stake);

	}

	if (Pipe::PushRemote0::Flags::HdrsDown & r.m_Flags)
	{
		Env::Halt_if(!si.m_Dispute.m_Stake);

	}

	if (si.m_Dispute.m_Stake)
	{
		Pipe::UserInfo::Key kui2;
		Utils::Copy(kui2.m_Pk, si.m_Dispute.m_Winner);

		Pipe::UserInfo ui2;
		Env::LoadVar_T(kui2, ui2);

		Env::Halt_if(ui.m_Dispute.m_Work <= ui2.m_Dispute.m_Work);
	}

	if (bNew)
	{
		ui.m_Dispute.m_iIdx = si.m_Dispute.m_iIdx;

		Env::FundsLock(0, si.m_Cfg.m_StakeForRemote);
		Strict::Add(si.m_Dispute.m_Stake, si.m_Cfg.m_StakeForRemote);
	}
	Env::AddSig(r.m_User);

	kui.m_Type = Pipe::KeyType::UserInfo;
	Env::SaveVar_T(kui, ui);

	si.m_Dispute.m_Height = Env::get_Height();
	Utils::Copy(si.m_Dispute.m_Winner, r.m_User);

	Env::SaveVar_T(ki, si);
}

export void Method_5(const Pipe::FinalyzeRemote& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::Halt_if(!si.m_Dispute.m_Stake || (si.m_Dispute.m_Height - Env::get_Height() < si.m_Cfg.m_hDisputePeriod));

	Pipe::UserInfo::Key kui;
	kui.m_Type = Pipe::KeyType::UserInfo;
	Utils::Copy(kui.m_Pk, si.m_Dispute.m_Winner);

	uint32_t nSize = Env::LoadVar(&kui, sizeof(kui), nullptr, 0);
	auto pInpCp = (Pipe::InpCheckpointHdr*) Env::StackAlloc(sizeof(Pipe::InpCheckpointHdr) + nSize);
	Utils::Copy(pInpCp->m_User, si.m_Dispute.m_Winner);

	Env::LoadVar(&kui, sizeof(kui), pInpCp + 1, nSize);
	Env::DelVar_T(kui);

	Pipe::InpCheckpointHdr::Key cpk;
	cpk.m_iCheckpoint = si.m_Dispute.m_iIdx;
	Env::SaveVar(&cpk, sizeof(cpk), pInpCp, sizeof(Pipe::InpCheckpointHdr) + nSize);

	Env::FundsUnlock(0, si.m_Dispute.m_Stake);
	Env::AddSig(si.m_Dispute.m_Winner);

	Utils::ZeroObject(si.m_Dispute);
	si.m_Dispute.m_iIdx = cpk.m_iCheckpoint + 1;
	Env::SaveVar_T(ki, si);
}

export void Method_6(const Pipe::VerifyRemote0& r)
{
	uint32_t nSize = r.m_MsgSize + sizeof(Pipe::MsgHdr);
	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);

	if (r.m_Public)
		Utils::ZeroObject(pMsg->m_Receiver);
	else
		Env::get_CallerCid(1, pMsg->m_Receiver);

	Utils::Copy(pMsg->m_Sender, r.m_Sender);
	pMsg->m_Height = r.m_Height;

	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	HashValue hv;
	get_MsgHash(hv, pMsg, nSize);

	Env::StackFree(nSize);

	nSize = sizeof(Pipe::InpCheckpointHdr) + sizeof(HashValue) * (r.m_iMsg + 1);
	auto pInpCp = (Pipe::InpCheckpointHdr*) Env::StackAlloc(nSize);

	Pipe::InpCheckpointHdr::Key cpk;
	cpk.m_iCheckpoint = r.m_iCheckpoint;

	auto nSizeActual = Env::LoadVar(&cpk, sizeof(cpk), pInpCp, nSize);
	Env::Halt_if(nSizeActual < nSize);

	auto* pHash = (const HashValue*) (pInpCp + 1);
	Env::Halt_if(Utils::Cmp(hv, pHash[r.m_iMsg]));

	// message verified
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::FundsLock(0, si.m_Cfg.m_ComissionPerMsg);

	Pipe::UserInfo ui;
	Pipe::UserInfo::Key kui;
	Utils::Copy(kui.m_Pk, pInpCp->m_User);

	if (!Env::LoadVar_T(kui, ui))
		Utils::ZeroObject(ui);

	Strict::Add(ui.m_Balance, si.m_Cfg.m_ComissionPerMsg);
	Env::SaveVar_T(kui, ui);
}
