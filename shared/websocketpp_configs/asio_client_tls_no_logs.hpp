#ifndef JWT_GAME_SERVER_ASIO_CLIENT_TLS_NO_LOGS_HPP
#define JWT_GAME_SERVER_ASIO_CLIENT_TLS_NO_LOGS_HPP

#include <websocketpp/config/asio_client.hpp>

struct asio_client_tls_no_logs : public websocketpp::config::asio_tls_client {
  using type = asio_client_tls_no_logs;
  using super = websocketpp::config::asio_tls_client;

  using concurrency_type = super::concurrency_type;

  using request_type = super::request_type;
  using response_type = super::response_type;

  using message_type = super::message_type;
  using con_msg_manager_type = super::con_msg_manager_type;
  using endpoint_msg_manager_type = super::endpoint_msg_manager_type;

  using alog_type = super::alog_type;
  using elog_type = super::elog_type;

  using rng_type = super::rng_type;

  struct transport_config : public super::transport_config {
    using concurrency_type = type::concurrency_type;
    using alog_type = type::alog_type;
    using elog_type = type::elog_type;
    using request_type = type::request_type;
    using response_type = type::response_type;
    using socket_type = type::socket_type;
  };

  using transport_type =
    websocketpp::transport::asio::endpoint<transport_config>;

  static const websocketpp::log::level elog_level = 
    websocketpp::log::elevel::none;

  static const websocketpp::log::level alog_level =
    websocketpp::log::alevel::none;
};

#endif // JWT_GAME_SERVER_ASIO_CLIENT_TLS_NO_LOGS_HPP
