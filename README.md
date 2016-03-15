Building
=======================================================
Install the following libraries:

     sudo apt-get install protobuf-compiler libcrypto++9v5 libcrypto++9v5-dbg libcrypto++-dev libprotobuf9v5 libprotobuf-dev scons libnuma-dev libconfig-dev build-essential psmisc python-matplotlib

Get the source code:

    git clone git://github.com/moritzraho/logcabin.git
    cd logcabin
    git submodule update --init

To build for IX set the following variable in `Local.sc`. If set to 0 the build will be done for Linux:

    IX=1

Build:

    scons

In a separate directory clone and build IX, refer to
https://github.com/ix-project/ix.

Starting a cluster and running the Benchmark
===============================================

Create a file `logcabin-1.conf`:

    serverId = 1 #should be unique for every machine in the cluster
    listenAddresses = 192.168.17.1:1234 #should be the same address as in ix.conf
    snapshotWatchdogMilliseconds = 0

For running on Linux:

    rm -rf storage
    build/LogCabin --config logcabin-1.conf --bootstrap

Removing the storage cleans any previous instance of the cluster.
The server with ID 1 will now have a valid cluster membership configuration in
its log. At this point, there's only 1 server in the cluster, so only 1 vote is
needed: it'll be able to elect itself leader and commit new entries. We can now
start this server (leave it running):

    build/LogCabin --config logcabin-1.conf

Otherwise, for running LogCabin on IX, do:

    cd path/to/ix
    sudo rm -rf storage
    sudo ./dp/ix -l 5 -- path/to/logcabin/build/LogCabin --config path/to/logcabin/logcabin-1.conf --bootstrap
    (CTRL + C)
    sudo ./dp/ix -l 5 -- path/to/logcabin/build/LogCabin --config path/to/logcabin/logcabin-1.conf

To start other machines repeat these steps *without bootstrapping* and by changing
the serverId and address in the `logcabin-1.conf` file.

Now build LogCabin for Linux on another machine and use the reconfiguration
command to add the servers to the cluster:

    ALLSERVERS=192.168.17.1:1234,ip:port,...
    build/Examples/Reconfigure --cluster=$ALLSERVERS set ip:port ip:port ...

We will run the benchmarks from this same machine,
first add this to scripts/localconfig.py:

    smokehosts = hosts = [
        ('192.168.17.1', '192.168.17.1:1234', 1),
        ('ip', 'ip:port', serverId),
        ...
    ]

Run the benchmark, be sure that psmisc is installed
(the script uses killall to kill background jobs):

    python scripts/bench_script.py

Results can be found in results/lat\_res#ofmachines and
results/rps\_res#ofmachines. A script plotting the results is located in
results/plot.py. To generate a graph, modify the RES_FILES and LABELS
variables in plot.py and make sure that python-matplotlib is installed.

    cd results
    python plot.py

(for more details refer to the README in https://github.com/logcabin/logcabin)
