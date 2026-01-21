import csv
import matplotlib.pyplot as plt

# ========== 读取 flow_stats.csv ==========
time = []

throughput_1 = []; lossrate_1 = []; rtt_1 = []; jitter_1 = []
throughput_2 = []; lossrate_2 = []; rtt_2 = []; jitter_2 = []
throughput_3 = []; lossrate_3 = []; rtt_3 = []; jitter_3 = []
throughput_4 = []; lossrate_4 = []; rtt_4 = []; jitter_4 = []

with open('flow_stats.csv', 'r') as file:
    reader = csv.reader(file)
    next(reader)
    for row in reader:
        time.append(float(row[0]))

        throughput_1.append(float(row[1]))
        lossrate_1.append(float(row[2]))
        rtt_1.append(float(row[3]))
        jitter_1.append(float(row[4]))

        throughput_2.append(float(row[5]))
        lossrate_2.append(float(row[6]))
        rtt_2.append(float(row[7]))
        jitter_2.append(float(row[8]))

        throughput_3.append(float(row[9]))
        lossrate_3.append(float(row[10]))
        rtt_3.append(float(row[11]))
        jitter_3.append(float(row[12]))

        throughput_4.append(float(row[13]))
        lossrate_4.append(float(row[14]))
        rtt_4.append(float(row[15]))
        jitter_4.append(float(row[16]))

# ========== Marker 设置 ==========
markers = ['o', 'o', '^', 's']
markerfacecolors = ['black', 'white', 'black', 'black']

# ========== X轴刻度（2 秒）==========
step = 2
xticks_pos = list(range(int(max(time)) + 1))
xticks_label = [str(t) if t % step == 0 else '' for t in xticks_pos]

# ========== 创建 2×2 画布 ==========
fig, axs = plt.subplots(2, 2, figsize=(14,10))
fig.canvas.manager.set_window_title('')

def plot_on_axes(ax, data_list, ylabel, subtitle):
    """subtitle 用于放在图正下方"""
    ax.plot(time, data_list[0], label="Link 1", marker=markers[0],
            markerfacecolor=markerfacecolors[0], markersize=5)
    ax.plot(time, data_list[1], label="Link 2", marker=markers[1],
            markerfacecolor=markerfacecolors[1], markersize=5)
    ax.plot(time, data_list[2], label="Link 3", marker=markers[2],
            markerfacecolor=markerfacecolors[2], markersize=5)
    ax.plot(time, data_list[3], label="Link 4", marker=markers[3],
            markerfacecolor=markerfacecolors[3], markersize=5)

    ax.set_ylabel(ylabel)
    ax.set_xlabel('Time (s)')

    ax.set_xticks(xticks_pos)
    ax.set_xticklabels(xticks_label)

    # 自动适配Y轴范围，预留10%余量
    all_data = []
    for d in data_list:
        all_data.extend(d)
    if all_data:
        y_min = min(all_data)
        y_max = max(all_data)
        ax.set_ylim(y_min - 0.1*(y_max - y_min), y_max + 0.1*(y_max - y_min))

    ax.grid(True)
    ax.legend()

    # 把（a）~（d）写到图正下方
    ax.text(0.5, -0.15, subtitle, transform=ax.transAxes,
            ha='center', va='top', fontsize=12)

# ========== 绘制四张图 ==========
plot_on_axes(axs[0, 0],
             [throughput_1, throughput_2, throughput_3, throughput_4],
             "Throughput (Kbps)", "(a) Throughput")

plot_on_axes(axs[0, 1],
             [lossrate_1, lossrate_2, lossrate_3, lossrate_4],
             "Loss Rate (%)", "(b) Loss Rate")

plot_on_axes(axs[1, 0],
             [rtt_1, rtt_2, rtt_3, rtt_4],
             "Average RTT (ms)", "(c) Average RTT")

plot_on_axes(axs[1, 1],
             [jitter_1, jitter_2, jitter_3, jitter_4],
             "Jitter (ms)", "(d) Jitter")

# ========== 布局调整 & 保存 ==========
# ========== 布局调整 & 保存 ==========
plt.tight_layout(rect=[0, 0.08, 1, 1])   # 给底部留 8%
plt.subplots_adjust(hspace=0.4, bottom=0.12)
plt.savefig("flow_stats_merged.png", dpi=300, bbox_inches='tight')
plt.show()
