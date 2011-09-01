/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "ActiveSyncSource.h"
#include "ActiveSyncCalendarSource.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef ENABLE_UNIT_TESTS
# include <cppunit/extensions/TestFactoryRegistry.h>
# include <cppunit/extensions/HelperMacros.h>
#endif

#include <fstream>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe;

    isMe = sourceType.m_backend == "ActiveSync Address Book";
    if (isMe) {
        return
#ifdef ENABLE_ACTIVESYNC
            new ActiveSyncContactSource(params)
#else
            RegisterSyncSource::InactiveSource
#endif
            ;
    }

    isMe = sourceType.m_backend == "ActiveSync Events";
    if (isMe) {
        return
#ifdef ENABLE_ACTIVESYNC
            new ActiveSyncCalendarSource(params, EAS_ITEM_CALENDAR)
#else
            RegisterSyncSource::InactiveSource
#endif
            ;
    }

    isMe = sourceType.m_backend == "ActiveSync Todos";
    if (isMe) {
        return
#ifdef ENABLE_ACTIVESYNC
            new ActiveSyncCalFormatSource(params, EAS_ITEM_TODO)
#else
            RegisterSyncSource::InactiveSource
#endif
            ;
    }

    isMe = sourceType.m_backend == "ActiveSync Memos";
    if (isMe) {
        return
#ifdef ENABLE_ACTIVESYNC
            new ActiveSyncCalFormatSource(params, EAS_ITEM_JOURNAL)
#else
            RegisterSyncSource::InactiveSource
#endif
            ;
    }

    return NULL;
}

static RegisterSyncSource registerMe("ActiveSync",
#ifdef ENABLE_ACTIVESYNC
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "ActiveSync Address Book = eas-contacts\n"
                                     "ActiveSync Events = eas-events\n"
                                     "ActiveSync Todos = eas-todos\n"
                                     "ActiveSync Memos = eas-memos",
                                     Values() +
                                     (Aliases("ActiveSync Address Book") + "eas-contacts") +
                                     (Aliases("ActiveSync Events") + "eas-events") +
                                     (Aliases("ActiveSync Todos") + "eas-todos") +
                                     (Aliases("ActiveSync Memos") + "eas-memos"));

#ifdef ENABLE_ACTIVESYNC
#ifdef ENABLE_UNIT_TESTS

class ActiveSyncsTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ActiveSyncsTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("contacts", "ActiveSync Address Book", true));
        source.reset(SyncSource::createTestingSource("events", "ActiveSync Events", true));
        source.reset(SyncSource::createTestingSource("todos", "ActiveSync Todos", true));
        source.reset(SyncSource::createTestingSource("memos", "ActiveSync Memos", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(ActiveSyncsTest);

#endif // ENABLE_UNIT_TESTS

#ifdef ENABLE_INTEGRATION_TESTS
namespace {
#if 0
}
#endif

/**
 * Takes all existing items in the source and writes them into the file,
 * separated by a blank line. beginSync() with the previous sync key was
 * already called.
 *
 * Used for testing and thus should better not rely on cached information,
 * but ActiveSync doesn't offer an independent "list and/or retrieve all items"
 * operation. Using the cached information implies that we won't find bugs in
 * the handling of that information.
 */
static int DumpItems(ClientTest &client, TestingSyncSource &source, const char *file)
{
    ActiveSyncSource &eassource = static_cast<ActiveSyncSource &>(source);
    ofstream out(file);

    // find all ActiveSync server IDs: in ActiveSyncCalendarSource,
    // each server ID might appear multiple times, once for each
    // recurrence associated with it
    std::set<std::string> easids;
    BOOST_FOREACH (const std::string &luid, eassource.getAllItems()) {
        // slight hack: we know that luids in ActiveSyncSource base
        // class pass through this method unmodified, so no need to
        // avoid it
        StringPair ids = ActiveSyncCalendarSource::splitLUID(luid);
        easids.insert(ids.first);
    }

    BOOST_FOREACH(const std::string &easid, easids) {
        std::string item;
        eassource.ActiveSyncSource::readItem(easid, item);
        out << item << '\n';
        if (!boost::ends_with(item, "\n")) {
            out << '\n';
        }
    }
    return 0;
}

static TestingSyncSource *createEASSource(const ClientTestConfig::createsource_t &create,
                                          ClientTest &client, int source, bool isSourceA)
{
    TestingSyncSource *res = create(client, source, isSourceA);

    // Mangle username: if the base username in the config is account
    // "foo", then source B uses "foo_B", because otherwise it'll end
    // up sharing change tracking with source A.
    if (!isSourceA) {
        ActiveSyncSource *eassource = static_cast<ActiveSyncSource *>(res);
        std::string account = eassource->getSyncConfig().getSyncUsername();
        account += "_B";
        eassource->getSyncConfig().setSyncUsername(account, true);
    }

    if (boost::ends_with(res->getDatabaseID(), "_1")) {
        // only default database currently supported,
        // use that instead of first named database
        res->setDatabaseID("");
        return res;
    } else {
        // sorry, no database
        delete res;
        return NULL;
    }
}

// common settings for all kinds of data
static void updateConfigEAS(const RegisterSyncSourceTest */* me */,
                            ClientTestConfig &config)
{
        // cannot run tests involving a second database:
        // wrap orginal source creation, set default database for
        // database #0 and refuse to return a source for database #1
        config.createSourceA = boost::bind(createEASSource, config.createSourceA,
                                           _1, _2, _3);
        config.createSourceB = boost::bind(createEASSource, config.createSourceB,
                                           _1, _2, _3);

        config.dump = DumpItems;
        config.sourceLUIDsAreVolatile = true;
        // TODO: find out how ActiveSync/Exchange handle children without parent;
        // at the moment, the child is stored as if it was a stand-alone event
        // and the RECURRENCE-ID is lost (BMC #22831).
        config.linkedItemsRelaxedSemantic = false;
}

static class ActiveSyncContactTest : public RegisterSyncSourceTest {
public:
    ActiveSyncContactTest() :
        RegisterSyncSourceTest("eas_contact", // name of test => Client::Source::eas_contact"
                               "eds_contact"  // name of test cases: inherit from EDS, override below
                               ) {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        // override default eds_contact test config
        config.type = "eas-contacts";
        // TODO: provide comprehensive set of vCard 3.0 contacts as they are understood by the ActiveSync library
        // config.testcases = "testcases/eas_contact.vcf";

        updateConfigEAS(this, config);
    }
} ActiveSyncContactTest;

static class ActiveSyncEventTest : public RegisterSyncSourceTest {
public:
    ActiveSyncEventTest() :
        RegisterSyncSourceTest("eas_event", "eds_event")
    {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "eas-events";
        updateConfigEAS(this, config);
    }
} ActiveSyncEventTest;

static class ActiveSyncTodoTest : public RegisterSyncSourceTest {
public:
    ActiveSyncTodoTest() :
        RegisterSyncSourceTest("eas_task", "eds_task")
    {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "eas-todos";
        updateConfigEAS(this, config);
    }
} ActiveSyncTodoTest;

static class ActiveSyncMemoTest : public RegisterSyncSourceTest {
public:
    ActiveSyncMemoTest() :
        RegisterSyncSourceTest("eas_memo", "eds_memo")
    {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "eas-memos";
        updateConfigEAS(this, config);
    }
} ActiveSyncMemoTest;

} // anonymous namespace
#endif // ENABLE_INTEGRATION_TESTS

#endif // ENABLE_ACTIVESYNC

SE_END_CXX
