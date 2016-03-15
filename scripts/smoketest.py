#!/usr/bin/env python
# Copyright (c) 2012-2014 Stanford University
# Copyright (c) 2014-2015 Diego Ongaro
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

"""
This runs some basic tests against a LogCabin cluster.

Usage:
  smoketest.py [options]
  smoketest.py (-h | --help)

Options:
  -h --help            Show this help message and exit
  --binary=<cmd>       Server binary to execute [default: build/LogCabin]
  --client=<cmd>       Client binary to execute
                       [default: build/Examples/Benchmark]
  --reconf=<opts>      Additional options to pass through to the Reconfigure
                       binary. [default: '']
  --clientops=<opts>   Additional options for client binary. [default: '']
  --servers=<num>      Number of servers [default: 5]
  --it=<num>           Number of times to run client binary [default: 1]
  --sharedfs           Use this if the file system is shared.  

"""

from __future__ import print_function, division
from common import sh, captureSh, Sandbox, smokehosts
from docopt import docopt
import os
import random
import subprocess
import time

def main():
    arguments = docopt(__doc__)
    client_command = arguments['--client']
    server_command = arguments['--binary']
    num_servers = int(arguments['--servers'])
    reconf_opts = arguments['--reconf']
    if reconf_opts == "''":
        reconf_opts = ""
    client_opts = arguments['--clientops']
    if client_opts == "''":
        client_opts = ""

    it_num = int(arguments['--it'])
    max_ns = len(smokehosts)
    if max_ns < num_servers:
        print('Number of servers in config file: %s\n'%max_ns)
        num_servers = max_ns
    del max_ns
    
    sharedfs=False
    if arguments['--sharedfs']:
        sharedfs=True
    
    server_ids = range(1, num_servers + 1)
    cluster = "--cluster=%s" % ','.join([h[1] for h in
                                        smokehosts[:num_servers]])
    with Sandbox() as sandbox:
        sh('rm -rf debug/')
        sh('mkdir -p debug')
        sh('echo %s > debug/bench_cluster'%cluster)
        
        for server_id in server_ids:
            host = smokehosts[server_id - 1]
            with open('smoketest-%d.conf' % server_id, 'w') as f:
                f.write('serverId = %d\n' % server_id)
                f.write('listenAddresses = %s\n' % host[1])
                f.write('storagePath = %s'
                        %os.path.join(os.getcwd(),'teststorage/'))
                f.write('\n\n')
                try:
                    f.write(open('smoketest.conf').read())
                except:
                    pass

        if not sharedfs:
            print('Copying script files to remote servers',
                  'and removing previous storage')
            for server_id in server_ids:
                host = smokehosts[server_id - 1]
                sh('scp smoketest-%d.conf %s:%s'
                   %(server_id, host[0],os.getcwd()))
                sandbox.rsh(host[0], 'rm -rf teststorage')
            print()
        else:
            sh('rm -rf teststorage')
            
        print('Initializing first server\'s log')
        sandbox.rsh(smokehosts[0][0],
                    '%s --bootstrap --config smoketest-%d.conf' %
                    (server_command, server_ids[0]),
                    stderr=open('debug/bootstrap_err', 'w'))
        print()

        for server_id in server_ids:
            host = smokehosts[server_id - 1]
            command = ('%s --config smoketest-%d.conf' %
                       (server_command, server_id))
            print('Starting %s on %s' % (command, host[1]))
            sandbox.rsh(host[0], command, bg=True,
                        stderr=open('debug/%derr' % server_id, 'w'))
            sandbox.checkFailures()

        print('Growing cluster')
        #Carreful at verbosity here
        sh('build/Examples/Reconfigure --verbosity=ERROR %s %s set %s' %
           (cluster,
            reconf_opts,
            ' '.join([h[1] for h in smokehosts[:num_servers]])))

        print('Starting %s %s on localhost over %d iterations'
              % (client_command, cluster, it_num))
        print('\n')

#        time.sleep(100)
        
        for i in range(0,it_num):
            # client = sandbox.rsh('localhost',
            #                      ('%s %s %s'
            #                       % (client_command, client_opts, cluster)),
            #                      bg=False, stderr=open('debug/client', 'w')
            # )

            # small hack to redirect results to stderr for clean files
            sh(('%s %s %s 1>&2 2>/dev/null'
                % (client_command, client_opts, cluster)))


if __name__ == '__main__':
    main()
