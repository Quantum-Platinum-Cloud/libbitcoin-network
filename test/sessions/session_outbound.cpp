/**
 * Copyright (c) 2011-2021 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "../test.hpp"

struct session_outbound_tests_setup_fixture
{
    session_outbound_tests_setup_fixture()
    {
        test::remove(TEST_NAME);
    }

    ~session_outbound_tests_setup_fixture()
    {
        test::remove(TEST_NAME);
    }
};

BOOST_FIXTURE_TEST_SUITE(session_outbound_tests, session_outbound_tests_setup_fixture)

using namespace bc::network;
using namespace bc::network::messages;
using namespace bc::system::chain;

class mock_channel
  : public channel
{
public:
    typedef std::shared_ptr<mock_channel> ptr;

    mock_channel::mock_channel(bool& set, std::promise<bool>& coded,
        const code& match, socket::ptr socket, const settings& settings)
      : channel(socket, settings), match_(match), set_(set), coded_(coded)
    {
    }

    void stop(const code& ec) noexcept override
    {
        // Set future on first code match.
        if (ec == match_ && !set_)
        {
            set_ = true;
            coded_.set_value(true);
        }

        channel::stop(ec);
    }

private:
    const code match_;
    bool& set_;
    std::promise<bool>& coded_;
};

template <error::error_t ChannelStopCode = error::success>
class mock_connector_connect_success
  : public connector
{
public:
    typedef std::shared_ptr<mock_connector_connect_success> ptr;

    using connector::connector;

    // Require template parameterized channel stop code (ChannelStopCode).
    bool require_code() const
    {
        return coded_.get_future().get();
    }

    // Get captured connected.
    virtual bool connected() const
    {
        return !is_zero(connects_);
    }

    // Get captured hostname.
    virtual std::string hostname() const
    {
        return hostname_;
    }

    // Get captured port.
    virtual uint16_t port() const
    {
        return port_;
    }

    // Get captured stopped.
    virtual bool stopped() const
    {
        return stopped_;
    }

    // Capture stopped and free channel.
    void stop() override
    {
        stopped_ = true;
        connector::stop();
    }

    // Handle connect, capture first connected hostname and port.
    void connect(const std::string& hostname, uint16_t port,
        connect_handler&& handler) override
    {
        if (is_zero(connects_++))
        {
            hostname_ = hostname;
            port_ = port;
        }

        const auto socket = std::make_shared<network::socket>(service_);
        const auto channel = std::make_shared<mock_channel>(set_, coded_,
            ChannelStopCode, socket, settings_);

        // Must be asynchronous or is an infinite recursion.
        boost::asio::post(strand_, [=]()
        {
            // Connect result code is independent of the channel stop code.
            // As error code would set the re-listener timer, channel pointer is ignored.
            handler(error::success, channel);
        });
    }

protected:
    bool stopped_{ false };
    size_t connects_{ zero };
    std::string hostname_;
    uint16_t port_;
    bool set_{ false };
    mutable std::promise<bool> coded_;
};

class mock_connector_connect_fail
  : public connector
{
public:
    typedef std::shared_ptr<mock_connector_connect_fail> ptr;

    using connector::connector;

    void connect(const std::string&, uint16_t,
        connect_handler&& handler) override
    {
        handler(error::invalid_magic, nullptr);
    }
};

class mock_session_outbound
  : public session_outbound
{
public:
    using session_outbound::session_outbound;

    bool inbound() const noexcept override
    {
        return session_outbound::inbound();
    }

    bool notify() const noexcept override
    {
        return session_outbound::notify();
    }

    bool stopped() const
    {
        return session_outbound::stopped();
    }

    // Capture first start_connect call.
    void start_connect(connectors_ptr connectors) noexcept override
    {
        // Must be first to ensure connector::connect() preceeds promise release.
        session_outbound::start_connect(connectors);

        if (is_one(connects_))
            reconnect_.set_value(true);

        if (is_zero(connects_++))
            connect_.set_value(true);
    }

    bool connected() const
    {
        return !is_zero(connects_);
    }

    bool require_connected() const
    {
        return connect_.get_future().get();
    }

    bool require_reconnect() const
    {
        return reconnect_.get_future().get();
    }

    void attach_handshake(const channel::ptr&,
        result_handler handshake) const noexcept override
    {
        if (!handshaked_)
        {
            handshaked_ = true;
            handshake_.set_value(true);
        }

        // Simulate handshake successful completion.
        handshake(error::success);
    }

    bool attached_handshake() const
    {
        return handshaked_;
    }

    bool require_attached_handshake() const
    {
        return handshake_.get_future().get();
    }

protected:
    mutable bool handshaked_{ false };
    mutable std::promise<bool> handshake_;

private:
    code connect_code_{ error::success };
    size_t connects_{ zero };
    mutable std::promise<bool> connect_;
    mutable std::promise<bool> reconnect_;
};

class mock_session_outbound_one_address_count
  : public mock_session_outbound
{
public:
    using mock_session_outbound::mock_session_outbound;

    size_t address_count() const noexcept override
    {
        return 1;
    }
};

class mock_session_outbound_one_address
  : public mock_session_outbound_one_address_count
{
public:
    typedef std::shared_ptr<mock_session_outbound_one_address> ptr;

    using mock_session_outbound_one_address_count::
        mock_session_outbound_one_address_count;

    void fetch(hosts::address_item_handler handler) const noexcept override
    {
        handler(error::success, address_item{});
    }
};

class mock_session_outbound_one_address_blacklisted
  : public mock_session_outbound_one_address
{
public:
    using mock_session_outbound_one_address::mock_session_outbound_one_address;

    bool blacklisted(const config::authority&) const noexcept override
    {
        return true;
    }
};

class mock_connector_stop_connect
  : public mock_connector_connect_success<error::service_stopped>
{
public:
    typedef std::shared_ptr<mock_connector_stop_connect> ptr;

    mock_connector_stop_connect(asio::strand& strand, asio::io_context& service,
        const settings& settings, mock_session_outbound::ptr session)
      : mock_connector_connect_success(strand, service, settings),
        session_(session)
    {
    }

    void connect(const std::string& hostname, uint16_t port,
        connect_handler&& handler) noexcept override
    {
        BC_ASSERT_MSG(session_, "call set_session");

        // This connector.connect is invoked from network stranded method.
        session_->stop();

        mock_connector_connect_success<error::service_stopped>::connect(hostname,
            port, std::move(handler));
    }

private:
    mock_session_outbound::ptr session_;
};

template <class Connector = network::connector>
class mock_p2p
  : public p2p
{
public:
    using p2p::p2p;

    // Get last created connector.
    typename Connector::ptr get_connector() const
    {
        return connector_;
    }

    // Create mock connector to inject mock channel.
    connector::ptr create_connector() noexcept override
    {
        return ((connector_ = std::make_shared<Connector>(strand(), service(),
            network_settings())));
    }

private:
    typename Connector::ptr connector_;
};

template <class Connector = network::connector>
class mock_p2p_one_address_count
  : public mock_p2p<Connector>
{
public:
    using mock_p2p::mock_p2p;

    size_t address_count() const override
    {
        return 1;
    }
};


template <class Connector = network::connector>
class mock_p2p_one_address
  : public mock_p2p_one_address_count<Connector>
{
public:
    using mock_p2p_one_address_count::mock_p2p_one_address_count;

    void fetch(hosts::address_item_handler handler) const override
    {
        handler(error::success, address_item{});
    }
};

// Can't derive from mock_p2p because Connector has more arguments.
class mock_p2p_stop_connect
  : public p2p
{
public:
    using p2p::p2p;

    void set_session(mock_session_outbound::ptr session)
    {
        session_ = session;
    }

    // Get first created connector.
    mock_connector_stop_connect::ptr get_connector() const
    {
        return connector_;
    }

    // Create mock connector to inject mock channel.
    connector::ptr create_connector() noexcept override
    {
        if (connector_)
            return connector_;

        return ((connector_ = std::make_shared<mock_connector_stop_connect>(
            strand(), service(), network_settings(), session_)));
    }

private:
    mock_connector_stop_connect::ptr connector_;
    mock_session_outbound::ptr session_;
};

// properties

BOOST_AUTO_TEST_CASE(session_outbound__inbound__default__false)
{
    settings set(selection::mainnet);
    p2p net(set);
    mock_session_outbound session(net);
    BOOST_REQUIRE(!session.inbound());
}

BOOST_AUTO_TEST_CASE(session_outbound__notify__default__true)
{
    settings set(selection::mainnet);
    p2p net(set);
    mock_session_outbound session(net);
    BOOST_REQUIRE(session.notify());
}

// stop

BOOST_AUTO_TEST_CASE(session_outbound__stop__started__stopped)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    set.connect_batch_size = 1;
    set.outbound_connections = 1;
    p2p net(set);
    auto session = std::make_shared<mock_session_outbound_one_address_count>(net);
    BOOST_REQUIRE(session->stopped());

    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    // This indicates successful start, not connection(s) status.
    // Because p2p is not started, connections will fail until stop.
    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(!session->stopped());

    std::promise<bool> stopped;
    boost::asio::post(net.strand(), [=, &stopped]()
    {
        session->stop();
        stopped.set_value(true);
    });

    BOOST_REQUIRE(stopped.get_future().get());
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

BOOST_AUTO_TEST_CASE(session_outbound__stop__stopped__stopped)
{
    settings set(selection::mainnet);
    p2p net(set);
    mock_session_outbound session(net);

    std::promise<bool> promise;
    boost::asio::post(net.strand(), [&]()
    {
        session.stop();
        promise.set_value(true);
    });

    BOOST_REQUIRE(promise.get_future().get());
    BOOST_REQUIRE(session.stopped());
}

// start

BOOST_AUTO_TEST_CASE(session_outbound__start__no_outbound_connections__stopped)
{
    settings set(selection::mainnet);
    set.outbound_connections = 0;
    set.host_pool_capacity = 1;
    p2p net(set);
    auto session = std::make_shared<mock_session_outbound>(net);
    BOOST_REQUIRE(session->stopped());

    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

BOOST_AUTO_TEST_CASE(session_outbound__start__no_host_pool_capacity__stopped)
{
    settings set(selection::mainnet);
    p2p net(set);
    auto session = std::make_shared<mock_session_outbound>(net);
    BOOST_REQUIRE(session->stopped());

    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

BOOST_AUTO_TEST_CASE(session_outbound__start__no_addresses__stopped)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    p2p net(set);
    auto session = std::make_shared<mock_session_outbound>(net);
    BOOST_REQUIRE(session->stopped());

    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        // Will cause started to be set (only).
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::address_not_found);
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

BOOST_AUTO_TEST_CASE(session_outbound__start__restart__operation_failed)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    set.connect_batch_size = 1;
    set.outbound_connections = 1;
    p2p net(set);
    auto session = std::make_shared<mock_session_outbound_one_address_count>(net);
    BOOST_REQUIRE(session->stopped());

    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(!session->stopped());

    std::promise<code> restarted;
    boost::asio::post(net.strand(), [=, &restarted]()
    {
        session->start([&](const code& ec)
        {
            restarted.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(restarted.get_future().get(), error::operation_failed);
    BOOST_REQUIRE(!session->stopped());

    std::promise<bool> stopped;
    boost::asio::post(net.strand(), [=, &stopped]()
    {
        session->stop();
        stopped.set_value(true);
    });

    BOOST_REQUIRE(stopped.get_future().get());
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

// Connection errors get eaten with all connect failure codes (logging only).
BOOST_AUTO_TEST_CASE(session_outbound__start__three_outbound_three_batch__success)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    set.connect_batch_size = 3;
    set.outbound_connections = 3;
    set.connect_timeout_seconds = 10000;
    mock_p2p<> net(set);
    auto session = std::make_shared<mock_session_outbound_one_address>(net);
    BOOST_REQUIRE(session->stopped());
    
    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(!session->stopped());

    std::promise<bool> stopped;
    boost::asio::post(net.strand(), [=, &stopped]()
    {
        session->stop();
        stopped.set_value(true);
    });

    BOOST_REQUIRE(stopped.get_future().get());
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

// Blacklisting errors get eaten with all connect failure codes (logging only).
BOOST_AUTO_TEST_CASE(session_outbound__start__blacklisted__expected)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    set.connect_batch_size = 2;
    set.outbound_connections = 2;
    set.connect_timeout_seconds = 10000;
    mock_p2p<> net(set);
    auto session = std::make_shared<mock_session_outbound_one_address_blacklisted>(net);
    BOOST_REQUIRE(session->stopped());
   
    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(!session->stopped());

    std::promise<bool> stopped;
    boost::asio::post(net.strand(), [=, &stopped]()
    {
        session->stop();
        stopped.set_value(true);
    });

    BOOST_REQUIRE(stopped.get_future().get());
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

BOOST_AUTO_TEST_CASE(session_outbound__start__handle_connect_stopped__first_channel_service_stopped)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    set.connect_batch_size = 2;
    set.outbound_connections = 2;
    set.connect_timeout_seconds = 10000;

    // This invokes session.stop from within connect and then continues.
    // First channel is stopped for service_stopped and others for channel_dropped.
    mock_p2p_stop_connect net(set);
    auto session = std::make_shared<mock_session_outbound_one_address>(net);
    net.set_session(session);
    BOOST_REQUIRE(session->stopped());

    // Started session calls session.stop upon first connect.
    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started, &net]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(net.get_connector()->require_code());
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

BOOST_AUTO_TEST_CASE(session_outbound__start__handle_one__first_channel_success)
{
    settings set(selection::mainnet);
    set.host_pool_capacity = 1;
    set.connect_batch_size = 1;
    set.outbound_connections = 1;
    set.connect_timeout_seconds = 10000;

    // Started channel results in read failure.
    mock_p2p<mock_connector_connect_success<error::file_system>> net(set);
    auto session = std::make_shared<mock_session_outbound_one_address>(net);
    BOOST_REQUIRE(session->stopped());

    std::promise<code> started;
    boost::asio::post(net.strand(), [=, &started]()
    {
        session->start([&](const code& ec)
        {
            started.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(started.get_future().get(), error::success);
    BOOST_REQUIRE(!session->stopped());

    // Block until connected.
    BOOST_REQUIRE(session->require_connected());
    BOOST_REQUIRE(session->require_attached_handshake());

    // Block until handle_connect sets service_stopped in channel.stop.
    BOOST_REQUIRE(net.get_connector()->require_code());

    std::promise<bool> stopped;
    boost::asio::post(net.strand(), [=, &stopped]()
    {
        session->stop();
        stopped.set_value(true);
    });

    BOOST_REQUIRE(stopped.get_future().get());
    BOOST_REQUIRE(session->stopped());
    session.reset();
}

// start via network (not required for coverage)

BOOST_AUTO_TEST_CASE(session_outbound__start__network_started_no_outbound_connections__run_success)
{
    settings set(selection::mainnet);
    set.hosts_file = TEST_NAME;
    set.inbound_connections = 0;
    set.inbound_port = 0;
    set.seeds.clear();

    // Connect will return invalid_magic if executed, but this test will bypass it.
    set.outbound_connections = 0;
    mock_p2p<mock_connector_connect_fail> net(set);

    std::promise<code> start;
    std::promise<code> run;
    net.start([&](const code& ec)
    {
        start.set_value(ec);
        net.run([&](const code& ec)
        {
            run.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(start.get_future().get(), error::success);
    BOOST_REQUIRE_EQUAL(run.get_future().get(), error::success);
}

BOOST_AUTO_TEST_CASE(session_outbound__start__network_started_one_addresses_count__run_success)
{
    settings set(selection::mainnet);
    set.hosts_file = TEST_NAME;
    set.inbound_connections = 0;
    set.inbound_port = 0;
    set.seeds.clear();

    // Connect will return invalid_magic if executed, but this test will bypass it.
    set.host_pool_capacity = 1;
    mock_p2p_one_address_count<mock_connector_connect_fail> net(set);

    std::promise<code> start;
    std::promise<code> run;
    net.start([&](const code& ec)
    {
        start.set_value(ec);
        net.run([&](const code& ec)
        {
            run.set_value(ec);
        });
    });

    BOOST_REQUIRE_EQUAL(start.get_future().get(), error::success);
    BOOST_REQUIRE_EQUAL(run.get_future().get(), error::success);
}

BOOST_AUTO_TEST_CASE(session_outbound__start__network_started_one_addresses__run_success)
{
    settings set(selection::mainnet);
    set.hosts_file = TEST_NAME;
    set.inbound_connections = 0;
    set.inbound_port = 0;
    set.seeds.clear();

    // Connect will return invalid_magic when executed.
    set.host_pool_capacity = 1;
    set.connect_batch_size = 1;
    set.outbound_connections = 1;
    mock_p2p_one_address<mock_connector_connect_fail> net(set);

    std::promise<code> start;
    std::promise<code> run;
    net.start([&](const code& ec)
    {
        start.set_value(ec);
        net.run([&](const code& ec)
        {
            run.set_value(ec);
        });
    });
    
    // mock_connector_connect_fail configured to return invalid_magic, eaten by handle_connect.
    BOOST_REQUIRE_EQUAL(start.get_future().get(), error::success);
    BOOST_REQUIRE_EQUAL(run.get_future().get(), error::success);
}

BOOST_AUTO_TEST_SUITE_END()
