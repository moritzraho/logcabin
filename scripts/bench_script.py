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
  bench_script.py [options]
  bench_script.py (-h | --help)

Options:
  -h --help            Show this help message and exit
  --sharedfs           Use this if the file system is shared.                 
  --growing            Use this if you want to start and grow automatically the cluster (works only for linux).
"""

from __future__ import print_function, division
from common import sh, captureSh, Sandbox, smokehosts
from docopt import docopt
import os
import random
import subprocess
import time
import math

def lat_tp_test(client_command, cluster, num_servers):
    
    rps_res = "/home/raho/logcabin/results/rps_res%d"%num_servers
    lat_res = "/home/raho/logcabin/results/lat_res%d"%num_servers

    waits = [0, 2500, 5000, 10000, 25000, 50000, 75000, 100000]
    #[0, 50, 100, 250, 500, 1000, 5000, 10000, 20000, 30000]    
    opts = "--timeout=0 --writes=1600000 --size=1024 --threads=250"
    lat_opts = "--writes=100000 --size=1024 --wait=500000 --threads=1"

    binary = os.path.basename(client_command) 
    sh('echo start >%s'%rps_res)
    sh('echo start >%s'%lat_res)
    for w in waits:
        # to be determined better
        timeout = 15

        print('wait with %dus'%w)
        #Run all threads for throughput
        sh('echo wait with %dus >>%s'%(w,rps_res))
        sh(('%s %s --wait=%d %s 1>>%s 2>/dev/null &'
            % (client_command, opts, w, cluster,
               rps_res
            )))
        
        time.sleep(1)
        sh('echo wait with %dus >>%s'%(w,lat_res))
        sh(('%s %s --timeout=%d %s  1>>%s 2>/dev/null'
            % (client_command, lat_opts, timeout, cluster,
              lat_res
            )))

        sh(('killall %s'% binary))
        if w is not waits[-1]:
            print('sleeping..')
            time.sleep(6)
        
    sh('echo end >>%s'%rps_res)
    sh('echo end >>%s'%lat_res)

def main():
    arguments = docopt(__doc__)

    growing=False
    if arguments['--growing']:
        growing=True

    sharedfs=False
    if arguments['--sharedfs']:
        sharedfs=True
    
    client_command = 'build/Examples/Benchmark'
    server_command = 'build/LogCabin'

    num_servers = len(smokehosts)
    

    server_ids = range(1, num_servers + 1)
    cluster = "--cluster=%s" % ','.join([h[1] for h in
                                        smokehosts[:num_servers]])

    with Sandbox() as sandbox:
        sh('rm -rf debug/')
        sh('mkdir -p debug')
        if growing:
            for server_id in server_ids:
                host = smokehosts[server_id - 1]
                with open('benchtest-%d.conf' % server_id, 'w') as f:
                    f.write('serverId = %d\n' % server_id)
                    f.write('listenAddresses = %s\n' % host[1])
                    f.write('storagePath = %s'
                            %os.path.join(os.getcwd(),'teststorage/'))
                    f.write('\n\n')
                    try:
                        f.write(open('benchtest.conf').read())
                    except:
                        pass

            if not sharedfs:
                print('Copying script files to remote servers',
                      'and removing previous storage')
                for server_id in server_ids:
                    host = smokehosts[server_id - 1]
                    sh('scp benchtest-%d.conf %s:%s'
                       %(server_id, host[0],os.getcwd()))
                    sandbox.rsh(host[0], 'rm -rf teststorage')
                print()
            else:
                sh('rm -rf teststorage')

        
            print('Initializing first server\'s log')
            sandbox.rsh(smokehosts[0][0],
                        '%s --bootstrap --config benchtest-%d.conf' %
                        (server_command, server_ids[0]),
                        stderr=open('debug/bootstrap_err', 'w'))
            print()

        
            for server_id in server_ids:
                host = smokehosts[server_id - 1]
                command = ('%s --config benchtest-%d.conf' %
                           (server_command, server_id))
                print('Starting %s on %s' % (command, host[1]))
                sandbox.rsh(host[0], command, bg=True,
                            stderr=open('debug/%derr' % server_id, 'w'))
                sandbox.checkFailures()


            print('Growing cluster')
            #Carreful at verbosity here
            sh('build/Examples/Reconfigure --verbosity=ERROR %s set %s' %
               (cluster,
                ' '.join([h[1] for h in smokehosts[:num_servers]])))

        
        lat_tp_test(client_command, cluster, num_servers)
        #time.sleep(100)

if __name__ == '__main__':
    main()
