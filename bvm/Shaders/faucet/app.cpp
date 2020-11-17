#include "../common.h"
#include "contract.h"

struct DocGroup {
    DocGroup(const char* sz) {
        Env::DocAddGroup(sz);
    }
    ~DocGroup() {
        Env::DocCloseGroup();
    }
};

#define Faucet_manager_create(macro)
#define Faucet_manager_view(macro)
#define Faucet_manager_destroy(macro) macro(ContractID, cid)
#define Faucet_manager_view_accounts(macro) macro(ContractID, cid)

#define Faucet_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pubKey)

#define FaucetRole_manager(macro) \
    macro(manager, create) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_accounts) \
    macro(manager, view_account)

#define Faucet_my_account_view(macro) macro(ContractID, cid)

#define Faucet_my_account_deposit(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount) \
    macro(AssetID, aid)

#define Faucet_my_account_withdraw(macro) Faucet_my_account_deposit(macro)

#define FaucetRole_my_account(macro) \
    macro(my_account, view) \
    macro(my_account, deposit) \
    macro(my_account, withdraw)

#define FaucetRoles_All(macro) \
    macro(manager) \
    macro(my_account)

export void Method_0()
{
    // scheme
    {   DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { DocGroup grMethod(#name);  Faucet_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { DocGroup grRole(#name); FaucetRole_##name(THE_METHOD) }
        
        FaucetRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Faucet_##role##_##name(THE_FIELD) int unused = 0)



#pragma pack (push, 1)

struct KeyPrefix
{
    ContractID m_Cid;
    uint8_t m_Tag;
};

struct KeyRaw
{
    KeyPrefix m_Prefix;
    Faucet::Key m_Key;
};

#pragma pack (pop)

void EnumAndDump()
{
    while (true)
    {
        const KeyRaw* pRawKey;
        const Amount* pVal;

        uint32_t nKey, nVal;
        if (!Env::VarsMoveNext((const void**) &pRawKey, &nKey, (const void**) &pVal, &nVal))
            break;

        if ((sizeof(*pRawKey) == nKey) && (sizeof(*pVal) == nVal))
        {
            Env::DocAddGroup("elem");
            Env::DocAddBlob("Account", &pRawKey->m_Key.m_Account, sizeof(pRawKey->m_Key.m_Account));
            Env::DocAddNum("AssetID", pRawKey->m_Key.m_Aid);
            Env::DocAddNum("Amount", *pVal);
            Env::DocCloseGroup();
        }
    }
}

void DumpAccount(const PubKey& pubKey, const ContractID& cid)
{
    KeyRaw k0, k1;
    k0.m_Prefix.m_Cid = cid;
    k0.m_Prefix.m_Tag = 0;
    k0.m_Key.m_Account = pubKey;
    k0.m_Key.m_Aid = 0;

    Env::Memcpy(&k1, &k0, sizeof(k0));
    k1.m_Key.m_Aid = static_cast<AssetID>(-1);

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1));
    EnumAndDump();
}

ON_METHOD(manager, view)
{

#pragma pack (push, 1)
    struct Key {
        KeyPrefix m_Prefix;
        ContractID m_Cid;
    };
#pragma pack (pop)

    Key k0, k1;
    k0.m_Prefix.m_Cid = Faucet::s_SID;
    k0.m_Prefix.m_Tag = 0x10; // sid-cid tag
    k1.m_Prefix = k0.m_Prefix;

    Env::Memset(&k0.m_Cid, 0, sizeof(k0.m_Cid));
    Env::Memset(&k1.m_Cid, 0xff, sizeof(k1.m_Cid));

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1));

    while (true)
    {
        const Key* pKey;
        const void* pVal;
        uint32_t nKey, nVal;

        if (!Env::VarsMoveNext((const void**) &pKey, &nKey, &pVal, &nVal))
            break;

        if ((sizeof(Key) != nKey) || (1 != nVal))
            continue;

        Env::DocAddBlob("Cid", &pKey->m_Cid, sizeof(pKey->m_Cid));
    }
}

ON_METHOD(manager, create)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
}

ON_METHOD(manager, view_accounts)
{
    KeyPrefix k0, k1;
    k0.m_Cid = cid;
    k0.m_Tag = 0;
    k1.m_Cid = cid;
    k1.m_Tag = 1;

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1)); // enum all internal contract vars
    EnumAndDump();
}

ON_METHOD(manager, view_account)
{
    DumpAccount(pubKey, cid);
}

void DeriveMyPk(PubKey& pubKey, const ContractID& cid)
{
    Env::DerivePk(pubKey, &cid, sizeof(cid));
}

void On_MyAccount_MoveFunds(uint8_t nConsume, const ContractID& cid, Amount amount, AssetID aid)
{
    if (!amount)
    {
        Env::DocAddText("error", "amount should be nnz");
        return;
    }

    FundsChange fc;
    fc.m_Amount = amount;
    fc.m_Aid = aid;
    fc.m_Consume = nConsume;

    if (nConsume)
    {
        Faucet::Deposit arg;
        arg.m_Aid = fc.m_Aid;
        arg.m_Amount = fc.m_Amount;

        Env::GenerateKernel(&cid, Faucet::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, 1000000U);
    }
    else
    {
        Faucet::Withdraw arg;
        arg.m_Amount = fc.m_Amount;
        arg.m_Key.m_Aid = fc.m_Aid;
        DeriveMyPk(arg.m_Key.m_Account, cid);

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, Faucet::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, 2000000U);
    }
}

ON_METHOD(my_account, deposit)
{
    On_MyAccount_MoveFunds(1, cid, amount, aid);
}

ON_METHOD(my_account, withdraw)
{
    On_MyAccount_MoveFunds(0, cid, amount, aid);
}

ON_METHOD(my_account, view)
{
    PubKey pubKey;
    DeriveMyPk(pubKey, cid);
    DumpAccount(pubKey, cid);
}

#undef ON_METHOD
#undef THE_FIELD

export void Method_1()
{
    char szRole[0x10], szAction[0x10];
    ContractID cid;

    if (!Env::DocGetText("role", szRole, sizeof(szRole))) {
        Env::DocAddText("error", "Role not specified");
        return;
    }

    if (!Env::DocGetText("action", szAction, sizeof(szAction))) {
        Env::DocAddText("error", "Action not specified");
        return;
    }

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Faucet_##role##_##name(PAR_READ) \
            On_##role##_##name(Faucet_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        FaucetRole_##name(THE_METHOD) \
        Env::DocAddText("error", "invalid Action"); \
        return; \
    }

    FaucetRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    Env::DocAddText("error", "unknown Role");
}
