/**
 * Copyright (c) 2011-2023 libbitcoin developers (see AUTHORS)
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

BOOST_AUTO_TEST_SUITE(connector_tests)

class accessor
  : public connector
{
public:
    using connector::connector;

    const settings& get_settings() const NOEXCEPT
    {
        return settings_;
    }

    const asio::io_context& get_service() const NOEXCEPT
    {
        return service_;
    }

    const asio::strand& get_strand() const NOEXCEPT
    {
        return strand_;
    }

    deadline::ptr get_timer() NOEXCEPT
    {
        return timer_;
    }

    bool get_stopped() const NOEXCEPT
    {
        return !racer_.running();
    }
};

BOOST_AUTO_TEST_CASE(connector__construct__default__stopped_expected)
{
    const logger log{ false };
    threadpool pool(1);
    asio::strand strand(pool.service().get_executor());
    const settings set(bc::system::chain::selection::mainnet);
    auto instance = std::make_shared<accessor>(log, strand, pool.service(), set);

    BOOST_REQUIRE(&instance->get_settings() == &set);
    BOOST_REQUIRE(&instance->get_service() == &pool.service());
    BOOST_REQUIRE(&instance->get_strand() == &strand);
    BOOST_REQUIRE(instance->get_timer());
    BOOST_REQUIRE(instance->get_stopped());
}

class tiny_timeout
  : public settings
{
    using settings::settings;

    duration connect_timeout() const NOEXCEPT override
    {
        return microseconds(1);
    }
};

BOOST_AUTO_TEST_CASE(connector__connect_address__bogus_address__operation_timeout)
{
    const logger log{ false };
    threadpool pool(2);
    asio::strand strand(pool.service().get_executor());
    const tiny_timeout set(bc::system::chain::selection::mainnet);
    auto instance = std::make_shared<accessor>(log, strand, pool.service(), set);

    boost::asio::post(strand, [&]()
    {
        // DNS resolve failure (race), timeout includes a socket.
        instance->connect(config::address{ config::endpoint{ "42.42.42.42:42" }.to_address() },
            [](const code& ec, const socket::ptr& socket)
            {
                BOOST_REQUIRE_EQUAL(ec, error::operation_timeout);
                BOOST_REQUIRE(socket);
                BOOST_REQUIRE(socket->stopped());
            });

        std::this_thread::sleep_for(microseconds(1));
    });

    pool.stop();
    BOOST_REQUIRE(pool.join());

    BOOST_REQUIRE(instance->get_stopped());
}

BOOST_AUTO_TEST_CASE(connector__connect_authority__bogus_authority__operation_timeout)
{
    const logger log{ false };
    threadpool pool(2);
    asio::strand strand(pool.service().get_executor());
    const tiny_timeout set(bc::system::chain::selection::mainnet);
    auto instance = std::make_shared<accessor>(log, strand, pool.service(), set);

    boost::asio::post(strand, [instance]()
    {
        // IP address times out (never a resolve failure), timeout includes a socket.
        instance->connect(config::authority{ "42.42.42.42:42" },
            [](const code& ec, const socket::ptr& socket)
            {
                BOOST_REQUIRE_EQUAL(ec, error::operation_timeout);
                BOOST_REQUIRE(socket);
                BOOST_REQUIRE(socket->stopped());
            });

        std::this_thread::sleep_for(microseconds(1));
    });

    pool.stop();
    BOOST_REQUIRE(pool.join());

    BOOST_REQUIRE(instance->get_stopped());
}

BOOST_AUTO_TEST_CASE(connector__connect_endpoint__bogus_hostname__resolve_failed_race_operation_timeout)
{
    const logger log{ false };
    threadpool pool(2);
    asio::strand strand(pool.service().get_executor());
    const tiny_timeout set(bc::system::chain::selection::mainnet);
    auto instance = std::make_shared<accessor>(log, strand, pool.service(), set);

    boost::asio::post(strand, [instance]()
    {
        // DNS resolve failure (race), timeout includes a socket.
        instance->connect(config::endpoint{ "bogus.xxx", 42 },
            [](const code& ec, const socket::ptr& socket)
            {
                BOOST_REQUIRE((ec == error::resolve_failed && !socket) || (ec == error::operation_timeout && socket && socket->stopped()));
            });

        std::this_thread::sleep_for(microseconds(1));
    });

    pool.stop();
    BOOST_REQUIRE(pool.join());

    BOOST_REQUIRE(instance->get_stopped());
}

BOOST_AUTO_TEST_CASE(connector__connect__stop__resolve_failed_race_operation_canceled)
{
    const logger log{ false };
    threadpool pool(2);
    asio::strand strand(pool.service().get_executor());
    settings set(bc::system::chain::selection::mainnet);
    set.connect_timeout_seconds = 1000;
    auto instance = std::make_shared<accessor>(log, strand, pool.service(), set);

    boost::asio::post(strand, [instance]()
    {
        // DNS resolve failure (race), cancel may include a socket.
        instance->connect(config::endpoint{ "bogus.xxx", 42 },
            [](const code& ec, const socket::ptr& socket)
            {
                BOOST_REQUIRE(((ec == error::resolve_failed) && !socket) || (ec == error::operation_canceled));
            });

        std::this_thread::sleep_for(microseconds(1));
        instance->stop();
    });

    pool.stop();
    BOOST_REQUIRE(pool.join());

    BOOST_REQUIRE(instance->get_stopped());
}

BOOST_AUTO_TEST_CASE(connector__connect__started_start__operation_failed)
{
    const logger log{ false };
    threadpool pool(2);
    asio::strand strand(pool.service().get_executor());
    settings set(bc::system::chain::selection::mainnet);
    set.connect_timeout_seconds = 1000;
    auto instance = std::make_shared<accessor>(log, strand, pool.service(), set);

    boost::asio::post(strand, [instance]()
    {
        // DNS resolve failure (race), cancel may include a socket.
        instance->connect(config::endpoint{ "bogus.xxx", 42 },
            [](const code& ec, const socket::ptr& socket)
            {
                BOOST_REQUIRE(((ec == error::resolve_failed) && !socket) || (ec == error::operation_canceled));
            });
    
        // Connector is busy.
        instance->connect(config::endpoint{ "bogus.yyy", 24 },
            [](const code& ec, const socket::ptr& socket)
            {
                BOOST_REQUIRE(ec == error::operation_failed);
                BOOST_REQUIRE(!socket);
            });

        std::this_thread::sleep_for(microseconds(1));
        instance->stop();
    });

    pool.stop();
    BOOST_REQUIRE(pool.join());

    BOOST_REQUIRE(instance->get_stopped());
}

BOOST_AUTO_TEST_SUITE_END()
