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
    """Read CSV data and extract metrics for Link 1 and Link 3"""
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
# 修改：Mode 1→Before (原始配置)，Mode 2→After (优化后配置)
time_before, data_before = read_flow_stats('flow_stats2.csv')  # Before (Original) data
time_after, data_after = read_flow_stats('flow_stats3.csv')    # After (Optimized) data

# ========== 2. Style Configuration ==========
# Maintain color distinction between Link 1 and Link 3
configs = {
    0: {'label': 'Link 1', 'color': '#1f77b4', 'marker': 'o'},
    2: {'label': 'Link 3', 'color': '#2ca02c', 'marker': '^'}
}

# ========== 3. Plotting Function ==========
def plot_comparison(ax, metric_idx, ylabel, subtitle):
    for lid in [0, 2]:  # Iterate over Link 1 and Link 3
        cfg = configs[lid]
        
        # 修改：Router→Before (虚线+空心标记)
        ax.plot(time_before, data_before[lid][metric_idx], 
                label=f"{cfg['label']} (Before)",  # 标签更新为 Before
                color=cfg['color'], linestyle='--', alpha=0.6,
                marker=cfg['marker'], markerfacecolor='none', 
                markersize=6, linewidth=1.5)
        
        # 修改：Switch→After (实线+实心标记)
        ax.plot(time_after, data_after[lid][metric_idx], 
                label=f"{cfg['label']} (After)",   # 标签更新为 After
                color=cfg['color'], linestyle='-', 
                marker=cfg['marker'], markersize=6, linewidth=2.5)

    ax.set_ylabel(ylabel, fontsize=13, fontweight='bold')
    ax.set_xlabel('Time (s)', fontsize=13, fontweight='bold')
    ax.grid(True, linestyle=':', alpha=0.7, linewidth=0.8)
    
    # Set X-axis tick interval to 2 for better readability
    max_t = max(max(time_before), max(time_after))  # 变量更新为 before/after
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
# 修改：总标题更新为 Before-After 对比
fig.suptitle('Link Performance Comparison: Before vs After Optimization', 
             fontsize=16, fontweight='bold', y=0.98)

# Plot four metrics (保持不变)
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
# 修改：保存文件名更新为 Before-After 相关，方便区分
plt.savefig("before_after_link1_3_comparison.png", 
            dpi=300, 
            bbox_inches='tight',
            facecolor='white',  # White background (avoids gray in Word)
            edgecolor='none')
plt.show()