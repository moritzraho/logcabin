import numpy
import sys
import re
import os
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt

COLORS = numpy.array(["#B8C4B3","#82C464","#C37664","#64B2C3","#B7C478" ])

#number of machines
RES_FILES = [("lat_res3_linux", "rps_res3_linux"), ("lat_res4_linux", "rps_res4_linux"),
             ("lat_res3_ix", "rps_res3_ix"), ("lat_res4_ix", "rps_res4_ix")]

# should be same size as res_files
LABELS = [("Linux: cluster of 3"), ("Linux: cluster of 4"), ("IX: cluster of 3"), ("IX: cluster of 4")]

"""
We need to extract the first column of rps_file for rps
And the second column of lat_file for lat

"""
def extract_both(lat_file, rps_file):

    lat_means, lat_stds = extract_one(lat_file, 1)
    rps_means, rps_stds = extract_one(rps_file, 0)

    return [lat_means, rps_means, lat_stds, rps_stds]


"""
A result file should look like:
wait with 0ns
240.954     2.1
240.677     2.0
240.427     2.3
239.289     2.1
wait with 100ns
140.954     4.1
140.677     4.0
140.427     4.3
139.289     4.1
end
"""
# which 0 for rps and 1 for lat
def extract_one(res_file, which):
    #return values (plot ready args)
    means = []
    stds = []

    tmp = numpy.array([])

    with open(res_file) as f:
        for line in f:
            if "wait with" in line or "end" in line:
                if tmp.any():
                    means.append(tmp.mean())
                    stds.append(tmp.std())
                tmp = numpy.array([])
           # elif not bool(re.search('\D', line)):
            elif not "start" in line:
                e = _extract_line(line, which)
                tmp = numpy.append(tmp, [e])

    return (means, stds)


# line is looking like "#RPS    #Lat"
# returns one of the two
def _extract_line(line, which):
    s = re.search('(\d*.\d*)\D*(\d*.\d*)', line)
    return float(s.group(which+1).strip())



def plot(lat, rps, lat_stds, rps_stds, labels):

    fig, ax = plt.subplots()

    plt.rcParams.update({'font.size': 35})

    #multipleplot for machines
    for i in range(len(lat)):
        ax.errorbar(
            rps[i], lat[i], xerr=rps_stds[i], yerr=lat_stds[i],
            capthick=2, capsize=5, lw=2, ecolor='black',
            fmt='-o', linewidth=3, color=COLORS[i],
            label=labels[i]
        )

    ## legend handling
    #remove errorbars
    handles, labels = ax.get_legend_handles_labels()
    handles = [h[0] for h in handles]

    l = ax.legend(handles, labels, loc='upper left', fontsize=30)
    # transparent frame
    f = l.get_frame()
    f.set_linewidth(0)
    f.set_alpha(0)



    ax.set_ylabel('Latency in ms')
    ax.set_xlabel('Requests per second')

    #sum(listoflist) flattens
    ax.set_xlim(xmin=min(sum(rps,[])) - max(sum(rps_stds,[])) -1)
    # Remove right and top axis
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    ax.tick_params(pad=15)

    # Grid below graph
    ax.yaxis.grid(color='gray', linestyle='-', linewidth=1, alpha=0.6)
    ax.set_axisbelow(True)
    # Transparent Background
    # fig.patch.set_alpha(0)
    # ax.patch.set_alpha(0)

    # Remove ugly ticks
    # fig.patch.set_visible(False)
    # for tic in ax.xaxis.get_major_ticks():
    #     tic.tick1On = tic.tick2On = False
    # for tic in ax.yaxis.get_major_ticks():
    #     tic.tick2On = False

    fig.set_size_inches(25.6, 14.4)
    #plt.show()
    plt.savefig('lat_rps.png', dpi=100)
    plt.close(fig)

if __name__ == "__main__":

    # Please handle multiple files args
    # res_file = None
    # if len(sys.argv) < 2:
    #     res_file = "results"
    # #elif sys.argv[1] = "-m":
    # else:
    #     res_file = sys.argv[1]

#    labels = []

    # containes all lat, rps means and lat, rps stds
    extracted = [[],[],[],[]]

    for rfs in RES_FILES:
        if not os.path.isfile(rfs[0]) and not os.path.isfile(rfs[1]):
            print('Not a file %s or %s'% (rfs))
            continue
            #raise ValueError('Not a file %s or %s'% (rfs))

#        labels.append("cluster of " + re.search('\D*(\d*)\D*', rfs[0]).group(1))
        e = extract_both(*rfs)
        for i in range(len(extracted)):
            extracted[i].append(e[i])

    print(extracted)
    plot(extracted[0],extracted[1],extracted[2],extracted[3], LABELS)
