#include <rai/cli/daemon.hpp>

#include <rai/working.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <fstream>
#include <thread>

rai_daemon::daemon_config::daemon_config () :
rpc_enable (false),
rpc_address (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ())),
rpc_enable_control (false)
{
}

void rai_daemon::daemon_config::serialize (std::ostream & output_a)
{
    boost::property_tree::ptree tree;
    tree.put ("rpc_address", rpc_address.to_string ());
    tree.put ("rpc_port", std::to_string (rpc_port));
    tree.put ("rpc_enable", rpc_enable);
    tree.put ("rpc_enable_control", rpc_enable_control);
	boost::property_tree::ptree node_l;
	node.serialize_json (node_l);
	tree.add_child ("node", node_l);
    boost::property_tree::write_json (output_a, tree);
}

rai_daemon::daemon_config::daemon_config (bool & error_a, std::istream & input_a)
{
    error_a = false;
    boost::property_tree::ptree tree;
    try
    {
        boost::property_tree::read_json (input_a, tree);
        auto rpc_address_l (tree.get <std::string> ("rpc_address"));
        auto rpc_port_l (tree.get <std::string> ("rpc_port"));
        rpc_enable = tree.get <bool> ("rpc_enable");
        rpc_enable_control = tree.get <bool> ("rpc_enable_control");
        auto bootstrap_peers_l (tree.get_child ("bootstrap_peers"));
		auto node_l (tree.get_child ("node"));
		error_a = error_a | node.deserialize_json (node_l);
        try
        {
            rpc_port = std::stoul (rpc_port_l);
            error_a = rpc_port > std::numeric_limits <uint16_t>::max ();
        }
        catch (std::logic_error const &)
        {
            error_a = true;
        }
        boost::system::error_code ec;
        rpc_address = boost::asio::ip::address_v6::from_string (rpc_address_l, ec);
        if (ec)
        {
            error_a = true;
        }
    }
    catch (std::runtime_error const &)
    {
        error_a = true;
    }
}

rai_daemon::daemon::daemon ()
{
}

void rai_daemon::daemon::run ()
{
    auto working (rai::working_path ());
	boost::filesystem::create_directories (working);
    auto config_error (false);
    rai_daemon::daemon_config config;
    auto config_path ((working / "config.json").string ());
    std::ifstream config_file;
    config_file.open (config_path);
    if (!config_file.fail ())
    {
        config = rai_daemon::daemon_config (config_error, config_file);
    }
    else
    {
        std::ofstream config_file;
        config_file.open (config_path);
        if (!config_file.fail ())
        {
            config.serialize (config_file);
        }
    }
    if (!config_error)
    {
        auto service (boost::make_shared <boost::asio::io_service> ());
        auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
        rai::processor_service processor;
        rai::node_init init;
        auto node (std::make_shared <rai::node> (init, service, working, processor, config.node));
        if (!init.error ())
        {
            node->start ();
            rai::rpc rpc (service, pool, config.rpc_address, config.rpc_port, *node, config.rpc_enable_control);
            if (config.rpc_enable)
            {
                rpc.start ();
            }
            std::thread network_thread ([&service] ()
                {
                    try
                    {
                        service->run ();
                    }
                    catch (...)
                    {
                        assert (false);
                    }
                });
            std::thread processor_thread ([&processor] ()
                {
                    try
                    {
                        processor.run ();
                    }
                    catch (...)
                    {
                        assert (false);
                    }
                });
            network_thread.join ();
            processor_thread.join ();
        }
        else
        {
            std::cerr << "Error initializing node\n";
        }
    }
    else
    {
        std::cerr << "Error loading configuration\n";
    }
}
