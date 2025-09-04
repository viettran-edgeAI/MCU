#!/usr/bin/env python3
"""
Benchmark Visualization for ID_vector Performance Comparison
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
from pathlib import Path

# Set style for better looking plots
plt.style.use('seaborn-v0_8')
sns.set_palette("husl")

def load_benchmark_data():
    """Load benchmark results from CSV file"""
    try:
        df = pd.read_csv('benchmark_results.csv')
        return df
    except FileNotFoundError:
        print("Error: benchmark_results.csv not found. Please run the benchmark first.")
        return None

def create_performance_comparison(df):
    """Create performance comparison charts"""
    fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
    
    # 1. Execution Time Comparison (log scale)
    test_names = [name.replace(' (BPV=1)', '').replace(' (BPV=2)', '') for name in df['Test_Name']]
    
    x = np.arange(len(test_names))
    width = 0.25
    
    ax1.bar(x - width, df['ID_vector_Time_ns'], width, label='ID_vector', alpha=0.8)
    ax1.bar(x, df['unordered_set_Time_ns'], width, label='unordered_set', alpha=0.8)
    ax1.bar(x + width, df['vector_Time_ns'], width, label='std::vector', alpha=0.8)
    
    ax1.set_yscale('log')
    ax1.set_xlabel('Test Cases')
    ax1.set_ylabel('Execution Time (nanoseconds)')
    ax1.set_title('Execution Time Comparison (Log Scale)')
    ax1.set_xticks(x)
    ax1.set_xticklabels(test_names, rotation=45, ha='right')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # 2. Memory Usage Comparison (log scale)
    ax2.bar(x - width, df['ID_vector_Memory_bytes'], width, label='ID_vector', alpha=0.8)
    ax2.bar(x, df['unordered_set_Memory_bytes'], width, label='unordered_set', alpha=0.8)
    ax2.bar(x + width, df['vector_Memory_bytes'], width, label='std::vector', alpha=0.8)
    
    ax2.set_yscale('log')
    ax2.set_xlabel('Test Cases')
    ax2.set_ylabel('Memory Usage (bytes)')
    ax2.set_title('Memory Usage Comparison (Log Scale)')
    ax2.set_xticks(x)
    ax2.set_xticklabels(test_names, rotation=45, ha='right')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    # 3. Speedup Factors
    ax3.bar(x - width/2, df['Speedup_vs_unordered_set'], width, label='vs unordered_set', alpha=0.8)
    ax3.bar(x + width/2, df['Speedup_vs_vector'], width, label='vs std::vector', alpha=0.8)
    
    ax3.set_xlabel('Test Cases')
    ax3.set_ylabel('Speedup Factor (times faster)')
    ax3.set_title('ID_vector Speedup Comparison')
    ax3.set_xticks(x)
    ax3.set_xticklabels(test_names, rotation=45, ha='right')
    ax3.legend()
    ax3.grid(True, alpha=0.3)
    
    # Add value labels on bars
    for i, (v1, v2) in enumerate(zip(df['Speedup_vs_unordered_set'], df['Speedup_vs_vector'])):
        ax3.text(i - width/2, v1 + 1, f'{v1:.1f}x', ha='center', va='bottom', fontsize=8)
        ax3.text(i + width/2, v2 + 1, f'{v2:.1f}x', ha='center', va='bottom', fontsize=8)
    
    # 4. Memory Efficiency Ratios
    ax4.bar(x - width/2, df['Memory_Ratio_vs_unordered_set'], width, label='vs unordered_set', alpha=0.8)
    ax4.bar(x + width/2, df['Memory_Ratio_vs_vector'], width, label='vs std::vector', alpha=0.8)
    
    ax4.set_xlabel('Test Cases')
    ax4.set_ylabel('Memory Ratio (ID_vector / other)')
    ax4.set_title('ID_vector Memory Efficiency')
    ax4.set_xticks(x)
    ax4.set_xticklabels(test_names, rotation=45, ha='right')
    ax4.legend()
    ax4.grid(True, alpha=0.3)
    
    # Add percentage labels
    for i, (v1, v2) in enumerate(zip(df['Memory_Ratio_vs_unordered_set'], df['Memory_Ratio_vs_vector'])):
        ax4.text(i - width/2, v1 + 0.05, f'{v1*100:.1f}%', ha='center', va='bottom', fontsize=8)
        ax4.text(i + width/2, v2 + 0.05, f'{v2*100:.1f}%', ha='center', va='bottom', fontsize=8)
    
    plt.tight_layout()
    plt.savefig('../images/performance_comparison.png', dpi=300, bbox_inches='tight')
    print("Performance comparison chart saved as 'performance_comparison.png'")
    return fig

def create_summary_statistics(df):
    """Create summary statistics visualization"""
    fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(14, 10))
    
    # 1. Average Performance Metrics
    metrics = ['Speed vs unordered_set', 'Speed vs std::vector', 
               'Memory vs unordered_set', 'Memory vs std::vector']
    
    speed_us_avg = df['Speedup_vs_unordered_set'].mean()
    speed_v_avg = df['Speedup_vs_vector'].mean()
    mem_us_avg = df['Memory_Ratio_vs_unordered_set'].mean()
    mem_v_avg = df['Memory_Ratio_vs_vector'].mean()
    
    values = [speed_us_avg, speed_v_avg, mem_us_avg, mem_v_avg]
    colors = ['skyblue', 'lightgreen', 'orange', 'pink']
    
    bars = ax1.bar(metrics, values, color=colors, alpha=0.8)
    ax1.set_title('Average Performance Metrics')
    ax1.set_ylabel('Factor / Ratio')
    ax1.tick_params(axis='x', rotation=45)
    
    # Add value labels
    for i, (bar, value) in enumerate(zip(bars, values)):
        height = bar.get_height()
        if 'Speed' in metrics[i]:
            ax1.text(bar.get_x() + bar.get_width()/2., height + 0.5, 
                    f'{value:.1f}x', ha='center', va='bottom', fontweight='bold')
        else:
            ax1.text(bar.get_x() + bar.get_width()/2., height + 0.02, 
                    f'{value:.3f}\n({value*100:.1f}%)', ha='center', va='bottom', fontweight='bold')
    
    # 2. Performance Distribution
    performance_data = []
    labels = []
    
    for _, row in df.iterrows():
        performance_data.extend([row['Speedup_vs_unordered_set'], row['Speedup_vs_vector']])
        labels.extend(['vs unordered_set', 'vs std::vector'])
    
    ax2.boxplot([df['Speedup_vs_unordered_set'], df['Speedup_vs_vector']], 
                labels=['vs unordered_set', 'vs std::vector'])
    ax2.set_title('Speedup Distribution')
    ax2.set_ylabel('Speedup Factor')
    ax2.grid(True, alpha=0.3)
    
    # 3. Memory Savings Pie Chart
    mem_savings_us = (1 - mem_us_avg) * 100
    mem_savings_v = (1 - mem_v_avg) * 100
    
    # vs unordered_set
    ax3.pie([mem_savings_us, 100 - mem_savings_us], 
            labels=[f'Memory Saved\n{mem_savings_us:.1f}%', f'Memory Used\n{100-mem_savings_us:.1f}%'],
            autopct='%1.1f%%', startangle=90, colors=['lightcoral', 'lightblue'])
    ax3.set_title('Memory Savings vs unordered_set')
    
    # vs std::vector
    if mem_savings_v > 0:
        ax4.pie([mem_savings_v, 100 - mem_savings_v], 
                labels=[f'Memory Saved\n{mem_savings_v:.1f}%', f'Memory Used\n{100-mem_savings_v:.1f}%'],
                autopct='%1.1f%%', startangle=90, colors=['lightgreen', 'lightyellow'])
    else:
        # Handle case where ID_vector uses more memory than std::vector
        extra_usage = abs(mem_savings_v)
        ax4.pie([100, extra_usage], 
                labels=[f'Base Memory\n100%', f'Extra Memory\n{extra_usage:.1f}%'],
                autopct='%1.1f%%', startangle=90, colors=['lightyellow', 'lightcoral'])
    ax4.set_title('Memory Usage vs std::vector')
    
    plt.tight_layout()
    plt.savefig('../images/summary_statistics.png', dpi=300, bbox_inches='tight')
    print("Summary statistics chart saved as 'summary_statistics.png'")
    return fig

def create_detailed_analysis(df):
    """Create detailed analysis charts"""
    fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 10))
    
    # 1. Time vs Memory Trade-off
    ax1.scatter(df['ID_vector_Memory_bytes'], df['ID_vector_Time_ns'], 
               s=100, alpha=0.7, label='ID_vector', color='blue')
    ax1.scatter(df['unordered_set_Memory_bytes'], df['unordered_set_Time_ns'], 
               s=100, alpha=0.7, label='unordered_set', color='red')
    ax1.scatter(df['vector_Memory_bytes'], df['vector_Time_ns'], 
               s=100, alpha=0.7, label='std::vector', color='green')
    
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    ax1.set_xlabel('Memory Usage (bytes)')
    ax1.set_ylabel('Execution Time (nanoseconds)')
    ax1.set_title('Time vs Memory Trade-off')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # 2. Efficiency Score (Speed improvement / Memory ratio)
    efficiency_us = df['Speedup_vs_unordered_set'] / df['Memory_Ratio_vs_unordered_set']
    efficiency_v = df['Speedup_vs_vector'] / df['Memory_Ratio_vs_vector']
    
    test_names = [name.replace(' (BPV=1)', '').replace(' (BPV=2)', '') for name in df['Test_Name']]
    x = np.arange(len(test_names))
    width = 0.35
    
    ax2.bar(x - width/2, efficiency_us, width, label='vs unordered_set', alpha=0.8)
    ax2.bar(x + width/2, efficiency_v, width, label='vs std::vector', alpha=0.8)
    
    ax2.set_xlabel('Test Cases')
    ax2.set_ylabel('Efficiency Score (Speed/Memory)')
    ax2.set_title('Overall Efficiency Score')
    ax2.set_xticks(x)
    ax2.set_xticklabels(test_names, rotation=45, ha='right')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    # 3. Performance Heatmap
    # Normalize values for better visualization
    norm_speed_us = df['Speedup_vs_unordered_set'] / df['Speedup_vs_unordered_set'].max()
    norm_speed_v = df['Speedup_vs_vector'] / df['Speedup_vs_vector'].max()
    norm_mem_us = (1 - df['Memory_Ratio_vs_unordered_set']) / (1 - df['Memory_Ratio_vs_unordered_set']).max()
    norm_mem_v = (1 - df['Memory_Ratio_vs_vector']) / abs(1 - df['Memory_Ratio_vs_vector']).max()
    
    heatmap_data = np.array([norm_speed_us, norm_speed_v, norm_mem_us, norm_mem_v]).T
    
    im = ax3.imshow(heatmap_data, cmap='RdYlGn', aspect='auto')
    ax3.set_xticks(range(4))
    ax3.set_xticklabels(['Speed vs US', 'Speed vs V', 'Mem Savings vs US', 'Mem Savings vs V'])
    ax3.set_yticks(range(len(test_names)))
    ax3.set_yticklabels(test_names)
    ax3.set_title('Normalized Performance Heatmap')
    
    # Add colorbar
    cbar = plt.colorbar(im, ax=ax3)
    cbar.set_label('Normalized Performance (0=worst, 1=best)')
    
    # 4. Cumulative advantages
    cumulative_speed_us = df['Speedup_vs_unordered_set'].cumsum()
    cumulative_speed_v = df['Speedup_vs_vector'].cumsum()
    
    x_range = range(len(df))
    ax4.plot(x_range, cumulative_speed_us, marker='o', label='Cumulative speedup vs unordered_set', linewidth=2)
    ax4.plot(x_range, cumulative_speed_v, marker='s', label='Cumulative speedup vs std::vector', linewidth=2)
    
    ax4.set_xlabel('Test Case Index')
    ax4.set_ylabel('Cumulative Speedup Factor')
    ax4.set_title('Cumulative Performance Advantages')
    ax4.legend()
    ax4.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('../images/detailed_analysis.png', dpi=300, bbox_inches='tight')
    print("Detailed analysis chart saved as 'detailed_analysis.png'")
    return fig

def generate_report(df):
    """Generate a text report with key insights"""
    report = []
    report.append("=" * 80)
    report.append("ID_VECTOR PERFORMANCE ANALYSIS REPORT")
    report.append("=" * 80)
    
    # Overall statistics
    avg_speed_us = df['Speedup_vs_unordered_set'].mean()
    avg_speed_v = df['Speedup_vs_vector'].mean()
    avg_mem_us = df['Memory_Ratio_vs_unordered_set'].mean()
    avg_mem_v = df['Memory_Ratio_vs_vector'].mean()
    
    report.append(f"\nOVERALL PERFORMANCE SUMMARY:")
    report.append(f"  Average speedup vs unordered_set: {avg_speed_us:.1f}x")
    report.append(f"  Average speedup vs std::vector:   {avg_speed_v:.1f}x")
    report.append(f"  Average memory ratio vs unordered_set: {avg_mem_us:.3f} ({avg_mem_us*100:.1f}%)")
    report.append(f"  Average memory ratio vs std::vector:   {avg_mem_v:.3f} ({avg_mem_v*100:.1f}%)")
    
    # Best performing scenarios
    best_speed_us_idx = df['Speedup_vs_unordered_set'].idxmax()
    best_speed_v_idx = df['Speedup_vs_vector'].idxmax()
    best_mem_us_idx = df['Memory_Ratio_vs_unordered_set'].idxmin()
    best_mem_v_idx = df['Memory_Ratio_vs_vector'].idxmin()
    
    report.append(f"\nBEST PERFORMANCE SCENARIOS:")
    report.append(f"  Best speedup vs unordered_set: {df.iloc[best_speed_us_idx]['Test_Name']} "
                 f"({df.iloc[best_speed_us_idx]['Speedup_vs_unordered_set']:.1f}x)")
    report.append(f"  Best speedup vs std::vector:   {df.iloc[best_speed_v_idx]['Test_Name']} "
                 f"({df.iloc[best_speed_v_idx]['Speedup_vs_vector']:.1f}x)")
    report.append(f"  Best memory efficiency vs unordered_set: {df.iloc[best_mem_us_idx]['Test_Name']} "
                 f"({df.iloc[best_mem_us_idx]['Memory_Ratio_vs_unordered_set']*100:.1f}%)")
    report.append(f"  Best memory efficiency vs std::vector:   {df.iloc[best_mem_v_idx]['Test_Name']} "
                 f"({df.iloc[best_mem_v_idx]['Memory_Ratio_vs_vector']*100:.1f}%)")
    
    # Key insights
    report.append(f"\nKEY INSIGHTS:")
    report.append(f"  • ID_vector consistently outperforms both alternatives in speed")
    report.append(f"  • Memory efficiency is exceptional vs unordered_set (avg {(1-avg_mem_us)*100:.1f}% savings)")
    
    if avg_mem_v < 1.0:
        report.append(f"  • Also memory efficient vs std::vector (avg {(1-avg_mem_v)*100:.1f}% savings)")
    else:
        report.append(f"  • Uses more memory than std::vector in some cases (trade-off for speed)")
    
    fastest_scenario = df.loc[df['ID_vector_Time_ns'].idxmin()]
    report.append(f"  • Fastest execution: {fastest_scenario['Test_Name']} ({fastest_scenario['ID_vector_Time_ns']:.0f} ns)")
    
    report.append(f"\nRECOMMENDATIONS:")
    report.append(f"  • Use ID_vector when memory is limited and IDs have known upper bounds")
    report.append(f"  • Particularly effective for sparse datasets with large ID ranges")
    report.append(f"  • BPV=1 for unique sets, BPV=2+ for multisets with count limits")
    
    report.append("=" * 80)
    
    # Save report
    with open('performance_report.txt', 'w') as f:
        f.write('\n'.join(report))
    
    print('\n'.join(report))
    print("\nDetailed report saved as 'performance_report.txt'")

def main():
    """Main function to generate all visualizations"""
    print("Loading benchmark data...")
    df = load_benchmark_data()
    
    if df is None:
        return
    
    print(f"Loaded {len(df)} benchmark results")
    print("\nGenerating visualizations...")
    
    # Create all visualizations
    fig1 = create_performance_comparison(df)
    fig2 = create_summary_statistics(df)
    fig3 = create_detailed_analysis(df)
    
    # Generate report
    generate_report(df)
    
    print("\n" + "="*60)
    print("VISUALIZATION COMPLETE!")
    print("Generated files:")
    print("  • performance_comparison.png - Main comparison charts")
    print("  • summary_statistics.png - Statistical analysis") 
    print("  • detailed_analysis.png - Advanced analysis")
    print("  • performance_report.txt - Detailed text report")
    print("="*60)
    
    # Display plots
    plt.show()

if __name__ == "__main__":
    main()
