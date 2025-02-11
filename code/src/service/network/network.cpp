﻿#include "network.h"

#include <thread>

ssim::network::network() :
    session_id_(0)
{
}

void ssim::network::init(std::shared_ptr<msg_route_interface> msg_route, uint16_t port /*= 9301*/, int therad_num /*= 1*/)
{
    // 初始化msg_route引用
    msg_route_ = msg_route;
    // 初始化asio相关组件
    ioc_ = std::make_shared<asio::io_context>(therad_num);
    acc_ = std::make_shared<tcp::acceptor>(*ioc_, tcp::endpoint(tcp::v4(), port));
    socket_ = std::make_shared<tcp::socket>(*ioc_);
}

void ssim::network::run()
{
    try {
        do_accept();
        std::thread t(&network::start_send_data, this);
        t.detach();
        ioc_->run();
    } catch (const std::exception& err) {
        std::cout << err.what() << std::endl;
    }
}

void ssim::network::do_accept()
{
    acc_->async_accept(*socket_,
        [this](std::error_code ec)
    {
        if (!ec) {
            std::make_shared<session>(create_session_id(), std::move(*socket_), this)->start();
        }

        do_accept();
    });
}

// 启动session_data发送循环
inline void ssim::network::start_send_data()
{
    while (start_send_.load()) {
        auto ret = pop_msg_send_queue();

        // msg_route接口保证数据存在
        assert(ret.p_data_);

        auto iter = get_sess(ret.session_id_);
        std::shared_ptr<session> sess = iter.lock();
        if (sess) {
            // 会话依然存在则发送
            sess->do_write_msg(ret.p_data_);
        } else {
            // 否则压入持久化队列
            push_msg_persistent_queue(ret.p_data_);
        }
    }
}

void ssim::network::push_msg_recv_queue(session_data data)
{
    msg_route_->push_msg_recv_queue(data);
}

ssim::session_data ssim::network::pop_msg_send_queue()
{
    return msg_route_->pop_msg_send_queue();
}

void ssim::network::push_msg_persistent_queue(std::shared_ptr<std::vector<uint8_t>> p_data)
{
    msg_route_->push_msg_persistent_queue(p_data);
}

void ssim::network::insert_sess(uint64_t sessid, std::weak_ptr<session> sess)
{
    std::lock_guard<std::shared_mutex> lk(sess_mu_);
    sess_.insert({ sessid, sess });
}

void ssim::network::remove_sess(uint64_t sessid)
{
    {
        // 移除会话表
        std::lock_guard<std::shared_mutex> lk(sess_mu_);
        sess_.erase(sessid);
    }
    
    // TODO:发送撤销登陆消息

}

std::weak_ptr<ssim::session> ssim::network::get_sess(uint64_t sessid)
{
    std::shared_lock<std::shared_mutex> lk(sess_mu_);
    auto iter = sess_.find(sessid);
    if (iter == sess_.end()) {
        return std::weak_ptr<ssim::session>();
    }
    return iter->second;
}

uint64_t ssim::network::create_session_id()
{
    uint64_t session_id;
    do {
        session_id = session_id_.load();
    } while (!session_id_.compare_exchange_weak(session_id, session_id + 1));
    return session_id;
}

SSIM_API std::shared_ptr<ssim::network_interface> ssim::create_network_interface()
{
    return std::make_shared<ssim::network>();
}
