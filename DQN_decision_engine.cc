#include "DQN_decision_engine.h"
#include "message.h"
#include "base_station.h"
#include "client_device.h"
#include "cloud_server.h"
#include "edge_device.h"
#include <functional> // std::bind_front

namespace okec
{

Env::Env(const device_cache& cache, const task& t, std::shared_ptr<DeepQNetwork> RL)
    : cache_(cache)
    , t_(t)
    , RL_(RL)
{
    // get observation(reset)
    state_.reserve(cache_.size());
    for (auto it = cache_.cbegin(); it != cache_.cend(); ++it)
        state_.push_back(TO_DOUBLE((*it)["cpu"])); // edge resources
}

auto Env::reset() -> torch::Tensor
{
    for (auto& t : t_.elements()) {
        t.set_header("status", "0"); // 0: 未处理 1: 已处理
    }

    // 任务属性也作为状态
    state_.push_back(std::stod(t_.at(0).get_header("cpu")));

    // torch::tensor makes a copy, from_blob does not (but torch::from_blob(vector).clone() does)
    auto observation = torch::from_blob(state_.data(), {static_cast<long>(state_.size())}, torch::kFloat64);
    return observation.unsqueeze(0);
}

auto Env::train() -> void
{
    if (t_.size() < 1)
        return;

    train_next(this->reset());
}

// 任务量特别大时，是否会存在不会调用 train_next 的情况？？？ 需要思考一下
auto Env::train_next(torch::Tensor observation) -> void
{
    static std::size_t step = 0;
    // std::cout << "observation:\n" << observation << "\n";
    // fmt::print("train task:\n {}\n", t_.dump(4));

    float reward;
    auto task_elements = t_.elements();
    if (auto it = std::ranges::find_if(task_elements, [](auto const& item) {
        return item.get_header("status") == "0";
    }); it != std::end(task_elements)) {
        auto action = RL_->choose_action(observation);
        // fmt::print("choose action: {}\n", action);

        auto& edge_cache = cache_.view();
        auto& server = edge_cache.at(action);
        // fmt::print("server:\n{}\n", server.dump(4));

        // 先获取当前最小的处理时间
        std::vector<double> time;
        for (auto& elem : task_elements) {
            auto pt = elem.get_header("processing_time");
            if (!pt.empty())
                time.push_back(std::stod(pt));
        }
        double min_time = 0;
        if (!time.empty())
            min_time = *std::ranges::min_element(time);

        // 获取每个边缘服务器处理该任务的时间
        std::vector<double> e_time;
        for (const auto& edge : edge_cache) {
            e_time.push_back(std::stod(it->get_header("cpu")) / TO_DOUBLE(edge["cpu"]));
        }
        // fmt::print("e time: {}\n", e_time);

        double e_average_time = std::accumulate(e_time.begin(), e_time.end(), .0) / edge_cache.size();


        //////////////////////////////////////////////////////////

        auto cpu_supply = TO_DOUBLE(server["cpu"]);
        auto cpu_demand = std::stod(it->get_header("cpu"));
        if (cpu_supply < cpu_demand) { // 无法处理
            reward = -e_average_time;
            RL_->store_transition(observation, action, reward, observation); // 状态不曾改变
            // fmt::print("reward: {}, done: {}\n", reward, false);

            // 超过200条transition之后每隔5步学习一次
            if (step > 200 and step % 5 == 0) {
                RL_->learn();
            }

            step++;
        } else { // 可以处理
            double new_cpu = cpu_supply - cpu_demand;
            // if (new_cpu > 2.0)
            //     reward = 1;
            // else if (new_cpu > 1.5)
            //     reward = 0.8;
            // else if (new_cpu > 1)
            //     reward = 0.5;
            // else if (new_cpu > 0.5)
            //     reward = 0.3;
            // else
            //     reward = 0.1;
            
            it->set_header("status", "1");

            // 消耗资源
            server["cpu"] = std::to_string(new_cpu);
            // fmt::print(fg(fmt::color::red), "[{}] 消耗资源：{} --> {}\n", TO_STR(server["ip"]), cpu_supply, TO_DOUBLE(server["cpu"]));

            // 处理时间
            double processing_time = cpu_demand / cpu_supply;
            
            // reward = min_time > 0 ? min_time - processing_time : 0;
            reward = e_average_time - processing_time;

            it->set_header("processing_time", std::to_string(processing_time));

            // 资源恢复
            auto self = shared_from_this();
            Simulator::Schedule(Seconds(processing_time), [self, action, cpu_demand]() {
                auto& edge_cache = self->cache_.view();
                auto& server = edge_cache.at(action);
                double cur_cpu = TO_DOUBLE(server["cpu"]);
                double new_cpu = cur_cpu + cpu_demand;
                // print_info(fmt::format("[{}] 恢复资源：{} --> {:.2f}(demand: {})", TO_STR(server["ip"]), cur_cpu, new_cpu, cpu_demand));
                
                // 恢复资源
                server["cpu"] = std::to_string(new_cpu);

                // 任务也作为状态
                auto task_elements = self->t_.elements();
                if (auto it = std::ranges::find_if(task_elements, [](auto const& item) {
                    return item.get_header("status") == "0";
                }); it != std::end(task_elements)) {
                    std::vector<double> flattened_state;
                    for (const auto& edge : edge_cache) {
                        flattened_state.push_back(TO_DOUBLE(edge["cpu"]));
                    }
                    flattened_state.push_back(std::stod(it->get_header("cpu")));
                    auto observation_new = torch::tensor(flattened_state, torch::dtype(torch::kFloat64)).unsqueeze(0);
                    self->train_next(observation_new); // 继续训练下一个
                }
            });


            // 结束或继续处理
            if (!t_.contains({"status", "0"})) {
                // fmt::print("reward: {}, done: {}\n", reward, true);


                // 胜利和失败奖励
                double total_time = .0f;
                for (auto& elem : t_.elements()) {
                    total_time += std::stod(elem.get_header("processing_time"));
                }
                if (total_time > 28) reward = -300;
                else if (total_time > 26) reward = -200;
                else if (total_time > 25) reward = 50;
                else if (total_time > 24) reward = 100;
                else if (total_time > 23) reward = 150;
                else reward = 200;

                std::vector<double> flattened_state;
                for (const auto& edge : edge_cache) {
                    flattened_state.push_back(TO_DOUBLE(edge["cpu"]));
                }
                flattened_state.push_back(std::stod(it->get_header("cpu")));
                auto observation_new = torch::tensor(flattened_state, torch::dtype(torch::kFloat64)).unsqueeze(0);
                RL_->store_transition(observation, action, reward, observation_new);


                if (done_fn_) {
                    done_fn_(t_, cache_);
                }
            } else {
                // fmt::print("reward: {}, done: {}\n", reward, false);
                // 更新状态
                std::vector<double> flattened_state;
                for (const auto& edge : edge_cache) {
                    flattened_state.push_back(TO_DOUBLE(edge["cpu"]));
                }
                // 任务也作为状态
                flattened_state.push_back(std::stod((++it)->get_header("cpu")));
                auto observation_new = torch::tensor(flattened_state, torch::dtype(torch::kFloat64)).unsqueeze(0);
                RL_->store_transition(observation, action, reward, observation_new);
                // std::cout << "new state\n" << observation_new << "\n";
                
                // exit(1);

                // 超过200条transition之后每隔5步学习一次
                if (step > 200 and step % 5 == 0) {
                    RL_->learn();
                }

                step++;

                this->train_next(observation_new); // 继续训练下一个
            }
        }
    }
}

auto Env::when_done(done_callback_t callback) -> void
{
    done_fn_ = callback;
}

auto Env::print_cache() -> void
{
    fmt::print("print_cache:\n{}\n", cache_.dump(4));
    fmt::print("tasks:\n{}\n", t_.dump(4));
}

DQN_decision_engine::DQN_decision_engine(
    client_device_container* clients,
    base_station_container* base_stations)
    : clients_{clients}
    , base_stations_{base_stations}
{
    // Set the decision device
    if (base_stations->size() > 0uz)
        m_decision_device = base_stations->get(0);

    // Initialize the device cache
    this->initialize_device(base_stations);

    // Capture the decision and response message on base stations.
    base_stations->set_request_handler(message_decision, std::bind_front(&this_type::on_bs_decision_message, this));
    base_stations->set_request_handler(message_response, std::bind_front(&this_type::on_bs_response_message, this));

    // Capture the handling message on edge servers
    base_stations->set_es_request_handler(message_handling, std::bind_front(&this_type::on_es_handling_message, this));

    // Capture the response message on client devices.
    clients->set_request_handler(message_response, std::bind_front(&this_type::on_clients_reponse_message, this));
}

DQN_decision_engine::DQN_decision_engine(
    std::vector<client_device_container>* clients_container,
    base_station_container* base_stations)
    : clients_container_{clients_container}
    , base_stations_{base_stations}
{
    // Set the decision device
    if (base_stations->size() > 0uz)
        m_decision_device = base_stations->get(0);

    // Initialize the device cache
    this->initialize_device(base_stations);

    // Capture the decision and response message on base stations.
    base_stations->set_request_handler(message_decision, std::bind_front(&this_type::on_bs_decision_message, this));
    base_stations->set_request_handler(message_response, std::bind_front(&this_type::on_bs_response_message, this));

    // Capture the handling message on edge servers
    base_stations->set_es_request_handler(message_handling, std::bind_front(&this_type::on_es_handling_message, this));

    // Capture the response message on client devices.
    for (auto& clients : *clients_container) {
        clients.set_request_handler(message_response, std::bind_front(&this_type::on_clients_reponse_message, this));
    }

    fmt::print("{}\n", this->cache().dump(4));
}

auto DQN_decision_engine::make_decision(const task_element& header) -> result_t
{
    return result_t();
}

auto DQN_decision_engine::local_test(const task_element& header, client_device* client) -> bool
{
    return false;
}

auto DQN_decision_engine::send(task_element& t, client_device* client) -> bool
{
    static double launch_delay = 1.0;

    client->response_cache().emplace_back({
        { "task_id", t.get_header("task_id") },
        { "group", t.get_header("group") },
        { "finished", "0" }, // 1 indicates finished, while 0 signifies the opposite.
        { "device_type", "" },
        { "device_address", "" },
        { "time_consuming", "" },
        { "send_time", "" },
        { "power_consumption", "" }
    });

    // 将所有任务都发送到决策设备，从而得到所有任务的信息
    // 追加任务发送地址信息
    t.set_header("from_ip", fmt::format("{:ip}", client->get_address()));
    t.set_header("from_port", std::to_string(client->get_port()));
    message msg;
    msg.type(message_decision);
    msg.content(t);
    const auto bs = this->get_decision_device();
    ns3::Simulator::Schedule(ns3::Seconds(launch_delay), &client_device::write, client, msg.to_packet(), bs->get_address(), bs->get_port());
    launch_delay += 0.1;

    return true;
}

auto DQN_decision_engine::train(const task& train_task, int episode) -> void
{
    auto n_actions = this->cache().size();
    auto n_features = this->cache().size() + 1; // +1 是 task cpu demand
    RL = std::make_shared<DeepQNetwork>(n_actions, n_features, 0.01, 0.9, 0.9, 300, 3000);

    train_start(train_task, episode, episode);


    // train_task = t;
    // auto n_actions = this->cache().size();
    // auto n_features = this->cache().size();
    // RL = std::make_shared<DeepQNetwork>(n_actions, n_features, 0.01, 0.9, 0.9, 200, 2000);

    // // get observation
    // std::vector<double> state;
    // state.reserve(this->cache().size());
    // for (auto it = this->cache().cbegin(); it != this->cache().cend(); ++it)
    //     state.push_back(TO_DOUBLE((*it)["cpu"])); // edge resources

    // auto observation = torch::tensor(state, torch::dtype(torch::kFloat64)).unsqueeze(0);

    // for (auto& t : train_task.elements()) {
    //     t.set_header("status", "0"); // 0: 未处理 1: 已处理
    // }

    // train_next(std::move(observation));




    // auto env = std::make_shared<Env>(this->cache(), t);

    // fmt::print(fg(fmt::color::yellow), "actions: {}, features: {}\n", env->n_actions(), env->n_features());

    // auto RL = DeepQNetwork(env->n_actions(), env->n_features(), 0.01, 0.9, 0.9, 200, 2000);
    
    // // run edge
    // int step = 0;

    // for ([[maybe_unused]] auto episode : std::views::iota(0, 1)) {
    //     // observation
    //     auto observation = env->reset();
    //     std::cout << observation << std::endl;

    //     for (;;) {
    //         // RL choose action based on observation
    //         auto action = RL.choose_action(observation);

    //         fmt::print("choose action: {}\n", action);

    //         auto [observation_, reward, done] = env->step2(action);
            
    //         // std::cout << "\n";
    //         // std::cout << observation_ << "\n";
    //         // std::cout << "reward: " << reward << " done: " << done << "\n";

    //         RL.store_transition(observation, action, reward, observation_);

    //         // 超过200条transition之后每隔5步学习一次
    //         if (step > 200 and step % 5 == 0) {
    //             RL.learn();
    //         }

    //         observation = observation_;

    //         if (done)
    //             break;

    //         step += 1;
    //     }
    // }

    // fmt::print("end of train\n");
    // // RL.print_memory();

    // env->print_cache();
    
    // // Simulator::Schedule(Seconds(1), [env]() {
    // //     env->print_cache();
    // // });
}

auto DQN_decision_engine::initialize() -> void
{
    if (clients_) {
        clients_->set_decision_engine(shared_from_base<this_type>());
    }

    if (clients_container_) {
        for (auto& clients : *clients_container_) {
            clients.set_decision_engine(shared_from_base<this_type>());
        }
    }

    if (base_stations_) {
        base_stations_->set_decision_engine(shared_from_base<this_type>());
    }
}

auto DQN_decision_engine::handle_next() -> void
{
    return void();
}

auto DQN_decision_engine::on_bs_decision_message(
    base_station* bs, Ptr<Packet> packet, const Address& remote_address) -> void
{
    ns3::InetSocketAddress inetRemoteAddress = ns3::InetSocketAddress::ConvertFrom(remote_address);
    print_info(fmt::format("The base station[{:ip}] has received the decision request from {:ip}.", bs->get_address(), inetRemoteAddress.GetIpv4()));

    auto item = okec::task_element::from_msg_packet(packet);
    bs->task_sequence(std::move(item));

    // bs->print_task_info();
    // bs->handle_next_task();
}

auto DQN_decision_engine::on_bs_response_message(
    base_station* bs, Ptr<Packet> packet, const Address& remote_address) -> void
{
}

auto DQN_decision_engine::on_cs_handling_message(
    cloud_server* cs, Ptr<Packet> packet, const Address& remote_address) -> void
{
}

auto DQN_decision_engine::on_es_handling_message(
    edge_device* es, Ptr<Packet> packet, const Address& remote_address) -> void
{
}

auto DQN_decision_engine::on_clients_reponse_message(
    client_device* client, Ptr<Packet> packet, const Address& remote_address) -> void
{
}

auto DQN_decision_engine::train_start(const task& train_task, int episode, int episode_all) -> void
{
    if (episode <= 0) {
        // RL->print_memory();
        return;
    }

    fmt::print(fg(fmt::color::blue), "At time {} seconds 正在训练第 {} 轮......\n", 
        Simulator::Now().GetSeconds(), episode_all - episode + 1);


    // 离散训练，必须每轮都创建一份对象，以隔离状态
    auto env = std::make_shared<Env>(this->cache(), train_task, RL);
    env->train();

    auto self = shared_from_base<this_type>();
    env->when_done([self, &train_task, episode, episode_all](const task& t, const device_cache& cache) {
        print_info("end of train"); // done
        double total_time = .0f;
        for (auto& elem : t.elements()) {
            total_time += std::stod(elem.get_header("processing_time"));
        }
        fmt::print(fg(fmt::color::orange), "total time: {} episode: {}\n", total_time, episode_all - episode + 1);
        // t.print();
        // fmt::print("cache: \n {}\n", cache.dump(4));
        
        // 继续训练下一轮
        self->train_start(train_task, episode - 1, episode_all);
    });
}



} // namespace okec