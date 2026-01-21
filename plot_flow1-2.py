import csv
import matplotlib.pyplot as plt

# ========== Global Font and Style Configuration ==========
# Set global font sizes for Word document compatibility
plt.rcParams.update({
    'font.size': 12,                # Base font size
    'axes.titlesize': 14,           # Subplot title size
    'axes.labelsize': 13,           # Axis label size
    'xtick.labelsize': 11,          # X-axis tick label size
    'ytick.labelsize': 11,          # Y-axis tick label size
    'legend.fontsize': 11,          # Legend font size
    'figure.titlesize': 16,         # Overall figure title size
    'font.family': 'Arial',         # Use Arial font (common in Word)
    'axes.unicode_minus': False     # Fix negative sign display issue
})

def read_flow_stats(filename):
    """Read CSV data and extract metrics for Link 1 and Link 2"""
    time = []
    # Structure: data[link_id] = {metric_idx: [values]}
    # Store only link 1 (idx 0) and link 3 (idx 2)
    selected_links = {0: [[] for _ in range(4)], 2: [[] for _ in range(4)]}
    
    with open(filename, 'r') as file:
        reader = csv.reader(file)
        next(reader)  # Skip header row
        for row in reader:
            time.append(float(row[0]))
            # Extract Link 1 (Columns 1-4)
            for m in range(4):
                selected_links[0][m].append(float(row[1 + m]))
            # Extract Link 3 (Columns 9-12)
            for m in range(4):
                selected_links[2][m].append(float(row[9 + m]))
    return time, selected_links

# ========== 1. Read Data ==========
# Mode 1: Router, Mode 2: Switch
time_router, data_router = read_flow_stats('flow_stats1.csv')  # Router data (Mode 1)
time_switch, data_switch = read_flow_stats('flow_stats2.csv')  # Switch data (Mode 2)

# ========== 2. Style Configuration ==========
# Maintain color distinction between Link 1 and Link 2
configs = {
    0: {'label': 'Link 1', 'color': '#1f77b4', 'marker': 'o'},
    2: {'label': 'Link 3', 'color': '#2ca02c', 'marker': '^'}
}

# ========== 3. Plotting Function ==========
def plot_comparison(ax, metric_idx, ylabel, subtitle):
    for lid in [0, 2]:   # Iterate over Link 1 and Link 2
        cfg = configs[lid]
        
        # Router (Mode 1): Dashed line + hollow markers
        ax.plot(time_router, data_router[lid][metric_idx], 
                label=f"{cfg['label']} (Router)", 
                color=cfg['color'], linestyle='--', alpha=0.6,
                marker=cfg['marker'], markerfacecolor='none', 
                markersize=6, linewidth=1.5)
        
        # Switch (Mode 2): Solid line + filled markers
        ax.plot(time_switch, data_switch[lid][metric_idx], 
                label=f"{cfg['label']} (Switch)", 
                color=cfg['color'], linestyle='-', 
                marker=cfg['marker'], markersize=6, linewidth=2.5)

    ax.set_ylabel(ylabel, fontsize=13, fontweight='bold')
    ax.set_xlabel('Time (s)', fontsize=13, fontweight='bold')
    ax.grid(True, linestyle=':', alpha=0.7, linewidth=0.8)
    
    # Set X-axis tick interval to 2 for better readability
    max_t = max(max(time_router), max(time_switch))
    ax.set_xticks(range(0, int(max_t) + 1, 2))
    ax.tick_params(axis='x', rotation=0)  # Horizontal x-ticks to avoid overlap
    
    # Optimize legend style and position
    ax.legend(loc='best', frameon=True, framealpha=0.9, 
              shadow=True, fancybox=True, ncol=1)
    ax.set_title(subtitle, y=-0.28, fontsize=14, fontweight='bold', pad=20)
    
    # Enhance axis border
    for spine in ax.spines.values():
        spine.set_linewidth(1.2)

# ========== 4. Create 2x2 Subplot Layout ==========
# Set figure size compatible with Word documents
fig, axs = plt.subplots(2, 2, figsize=(12, 10))
fig.suptitle('Link Performance Comparison: Router vs Switch', 
             fontsize=16, fontweight='bold', y=0.98)

# Plot four metrics
plot_comparison(axs[0, 0], 0, "Throughput (Kbps)", "(a) Throughput")
plot_comparison(axs[0, 1], 1, "Packet Loss Rate (%)", "(b) Packet Loss Rate")
plot_comparison(axs[1, 0], 2, "Average RTT (ms)", "(c) Average RTT")
plot_comparison(axs[1, 1], 3, "Jitter (ms)", "(d) Jitter")

# ========== 5. Layout Optimization ==========
plt.tight_layout()
plt.subplots_adjust(
    hspace=0.5,    # Vertical spacing
    wspace=0.3,    # Horizontal spacing
    bottom=0.12,   # Bottom margin
    top=0.93       # Top margin (for overall title)
)

# ========== 6. Save and Display ==========
# Save as high-resolution image for Word insertion
plt.savefig("router_vs_switch_link_comparison.png", 
            dpi=300, 
            bbox_inches='tight',
            facecolor='white',  # White background (avoids gray in Word)
            edgecolor='none')
plt.show()




# import csv
# import matplotlib.pyplot as plt

# def read_flow_stats(filename):
#     """读取 CSV 数据并提取 Link 1 和 Link 3 的指标"""
#     time = []
#     # 结构: data[link_id] = {metric_idx: [values]}
#     # 只存储 link 1 (idx 0) 和 link 3 (idx 2)
#     selected_links = {0: [[] for _ in range(4)], 2: [[] for _ in range(4)]}
    
#     with open(filename, 'r') as file:
#         reader = csv.reader(file)
#         next(reader)
#         for row in reader:
#             time.append(float(row[0]))
#             # 提取 Link 1 (Columns 1-4)
#             for m in range(4):
#                 selected_links[0][m].append(float(row[1 + m]))
#             # 提取 Link 3 (Columns 9-12)
#             for m in range(4):
#                 selected_links[2][m].append(float(row[9 + m]))
#     return time, selected_links

# # ========== 1. 读取数据 ==========
# # 请确保文件名与您的本地文件一致
# time_pre, data_pre = read_flow_stats('flow_stats1.csv')
# time_post, data_post = read_flow_stats('flow_stats2.csv')

# # ========== 2. 样式配置 ==========
# # 保持 Link 1 为蓝色，Link 3 为绿色
# configs = {
#     0: {'label': 'Link 1', 'color': '#1f77b4', 'marker': 'o'},
#     2: {'label': 'Link 3', 'color': '#2ca02c', 'marker': '^'}
# }

# # ========== 3. 绘图函数 ==========
# def plot_comparison(ax, metric_idx, ylabel, subtitle):
#     for lid in [0, 2]: # 只遍历 Link 1 和 Link 3
#         cfg = configs[lid]
        
#         # 修改前 (Before): 虚线 + 空心标记 + 较低透明度
#         ax.plot(time_pre, data_pre[lid][metric_idx], 
#                 label=f"{cfg['label']} (Original)", 
#                 color=cfg['color'], linestyle='--', alpha=0.4,
#                 marker=cfg['marker'], markerfacecolor='none', markersize=5)
        
#         # 修改后 (After): 实线 + 实心标记 + 高饱和度
#         ax.plot(time_post, data_post[lid][metric_idx], 
#                 label=f"{cfg['label']} (Optimized)", 
#                 color=cfg['color'], linestyle='-', linewidth=2,
#                 marker=cfg['marker'], markersize=6)

#     ax.set_ylabel(ylabel, fontsize=10)
#     ax.set_xlabel('Time (s)', fontsize=10)
#     ax.grid(True, linestyle=':', alpha=0.6)
    
#     # 设置 X 轴刻度步长为 2
#     max_t = max(max(time_pre), max(time_post))
#     ax.set_xticks(range(0, int(max_t) + 1, 2))
    
#     # 布局美化
#     ax.legend(fontsize=9, loc='best', frameon=True)
#     ax.set_title(subtitle, y=-0.25, fontsize=12, fontweight='bold')

# # ========== 4. 创建 2x2 画布 ==========
# fig, axs = plt.subplots(2, 2, figsize=(14, 10))

# plot_comparison(axs[0, 0], 0, "Throughput (Kbps)", "(a) Throughput")
# plot_comparison(axs[0, 1], 1, "Loss Rate (%)", "(b) Loss Rate")
# plot_comparison(axs[1, 0], 2, "Average RTT (ms)", "(c) Average RTT")
# plot_comparison(axs[1, 1], 3, "Jitter (ms)", "(d) Jitter")

# # ========== 5. 保存与展示 ==========
# plt.tight_layout()
# plt.subplots_adjust(hspace=0.4, bottom=0.15)
# plt.savefig("link_comparison_selective.png", dpi=300, bbox_inches='tight')
# plt.show()
