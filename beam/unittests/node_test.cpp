//#include "../node.h"

#include "../node_db.h"
#include "../../core/ecc_native.h"

namespace ECC {

	Context g_Ctx;
	const Context& Context::get() { return g_Ctx; }

}

#ifndef WIN32
#	include <unistd.h>
#endif // WIN32

int g_TestsFailed = 0;

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
	fflush(stdout);
}

void DeleteFile(const char* szPath)
{
#ifdef WIN32
	DeleteFileA(szPath);
#else // WIN32
	unlink(szPath);
#endif // WIN32
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

namespace beam
{

	void GetStateHash(Merkle::Hash& hash, Height h, uint8_t iBranch)
	{
		ECC::Hash::Processor() << h << iBranch >> hash;
	}

	void GetState(Block::SystemState::Full& s, Height h, uint8_t iBranch, uint8_t iBranchPrev)
	{
		s.m_Height = h;
		GetStateHash(s.m_Hash, h, iBranch);
		GetStateHash(s.m_HashPrev, h-1, iBranchPrev);
	}

	void TestNodeDB(const char* sz, bool bCreate)
	{
		NodeDB db;
		db.Open(sz, bCreate);

		NodeDB::Transaction tr(db);

		const uint32_t hMax = 1000;
		const uint32_t nOrd = 3;

		Block::SystemState::Full s;
		ZeroObject(s);

		uint64_t rowLast0 = 0, rowZero = 0;

		// insert states in random order
		for (uint32_t h1 = 0; h1 < nOrd; h1++)
		{
			for (uint32_t h = h1; h < hMax; h += nOrd)
			{
				GetState(s, h, 0, 0);
				uint64_t row = db.InsertState(s);

				if (hMax-1 == h)
					rowLast0 = row;

				if (h)
					db.SetStateFunctional(row);
				else
					rowZero = row;
			}
		}

		tr.Commit();
		tr.Start(db);

		// should only be 1 tip

		// a subbranch
		const uint32_t hFork0 = 700;

		GetState(s, hFork0, 1, 0);
		uint64_t r0 = db.InsertState(s); // should be 2 tips
		db.SetStateFunctional(r0);

		GetState(s, hFork0+1, 1, 1);
		uint64_t rowLast1 = db.InsertState(s); // still 2 tips
		db.SetStateFunctional(rowLast1);

		db.SetStateFunctional(rowZero); // this should trigger big update

		db.SetStateNotFunctional(rowZero);
		db.SetStateFunctional(rowZero);
		db.SetStateNotFunctional(rowZero);

		tr.Commit();
		tr.Start(db);

		// Delete main branch up to this tip
		uint32_t h = hMax;
		for (; ; h--)
		{
			assert(rowLast0);
			if (!db.DeleteIdleState(rowLast0, rowLast0))
				break;
		}

		assert(rowLast0 && (h == hFork0));

		for (h += 2; ; h--)
		{
			if (!rowLast1)
				break;
			verify_test(db.DeleteIdleState(rowLast1, rowLast1));
		}
		
		verify_test(!h);

		tr.Commit();
	}

	void TestNodeDB()
	{
#ifdef WIN32
		const char* sz = "mytest.db";
#else // WIN32
		const char* sz = "/tmp/mytest.db";
#endif // WIN32

		DeleteFile(sz);
		TestNodeDB(sz, true);
		TestNodeDB(sz, false);
		DeleteFile(sz);
	}

}

int main()
{
	beam::TestNodeDB();

    //beam::Node node;
    
    return 0;
}
