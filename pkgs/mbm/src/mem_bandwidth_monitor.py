#!/usr/bin/env python3
"""
Memory Bandwidth Monitor using perf
Uses LLC (Last Level Cache) events with continuous monitoring
"""

import subprocess
import time
import select
import matplotlib.pyplot as plt
import numpy as np
from datetime import datetime
import argparse
import sys
import signal
import threading
import shlex

class MemoryBandwidthMonitor:
    def __init__(self, interval=1.0, duration=None, command=None, title=None):
        """
        Initialize the memory bandwidth monitor
        
        Args:
            interval: Sampling interval in seconds
            duration: Total duration to monitor in seconds (None for infinite)
            command: Command to run and monitor (string or list)
            title: Custom title for the plot (None for auto-generated)
        """
        self.interval = interval
        self.duration = duration
        self.command = command
        self.title = title
        self.timestamps = []
        self.read_bandwidth = []  # GB/s
        self.write_bandwidth = []  # GB/s
        self.total_bandwidth = []  # GB/s
        self.running = True
        self.command_process = None
        self.command_returncode = None
        self.perf_process = None
        
        # Cache line size in bytes
        self.cache_line_size = 64
        
        # Memory bandwidth events
        self.read_event = None
        self.write_event = None
        self.events_detected = False
        
    def check_perf_availability(self):
        """Check if perf is available"""
        try:
            result = subprocess.run(['perf', 'list'], 
                                    capture_output=True, 
                                    text=True, 
                                    timeout=5)
            if result.returncode != 0:
                print("Error: perf command not available")
                return False
            return True
        except FileNotFoundError:
            print("Error: perf not installed. Install with: sudo apt install linux-tools-generic")
            return False
        except subprocess.TimeoutExpired:
            print("Error: perf command timed out")
            return False
    
    def detect_memory_events(self):
        """
        Auto-detect available memory bandwidth events
        Tries uncore memory controller events first, then falls back to LLC events
        """
        print("Auto-detecting memory bandwidth events...")
        
        # Priority 1: Uncore memory controller events (most accurate)
        # These are the actual DRAM traffic counters
        uncore_events = [
            # Intel uncore IMC CAS counts (your processor)
            ('unc_m_cas_count.rd', 'unc_m_cas_count.wr'),
            # Alternative naming with underscores
            ('uncore_imc/cas_count_read/', 'uncore_imc/cas_count_write/'),
            # With socket specification
            ('uncore_imc_0/cas_count_read/', 'uncore_imc_0/cas_count_write/'),
            # Raw event codes
            ('uncore_imc/event=0x04/', 'uncore_imc/event=0x0c/'),
        ]
        
        # Priority 2: LLC-based approach (fallback, most portable)
        llc_events = [
            ('LLC-loads', 'LLC-stores'),
            ('LLC-load-misses', 'LLC-store-misses'),
            ('cache-misses', 'cache-references'),
        ]
        
        # Try uncore events first
        print("\n  Trying uncore memory controller events (most accurate)...")
        for read_event, write_event in uncore_events:
            print(f"    Trying: {read_event}, {write_event}")
            
            if self._test_events(read_event, write_event):
                self.read_event = read_event
                self.write_event = write_event
                self.events_detected = True
                print(f"    ✓ Found working uncore events!")
                print(f"      Read:  {read_event}")
                print(f"      Write: {write_event}")
                print(f"\n    Note: Using uncore memory controller events")
                print(f"    These measure actual DRAM traffic (most accurate)")
                return True
        
        # Fall back to LLC events
        print("\n  Uncore events not available, falling back to LLC events...")
        for read_event, write_event in llc_events:
            print(f"    Trying: {read_event}, {write_event}")
            
            if self._test_events(read_event, write_event):
                self.read_event = read_event
                self.write_event = write_event
                self.events_detected = True
                print(f"    ✓ Found working LLC events!")
                print(f"      Read:  {read_event}")
                print(f"      Write: {write_event}")
                print(f"\n    Note: Using LLC events to estimate memory bandwidth")
                print(f"    This measures cache misses, which correlate with DRAM traffic")
                return True
        
        print("\n✗ Could not find working memory bandwidth events")
        print("\nTroubleshooting:")
        print("1. Try with sudo:")
        print("   sudo python mem_bandwidth_monitor.py ...")
        print("\n2. Adjust perf_event_paranoid:")
        print("   sudo sysctl kernel.perf_event_paranoid=-1")
        print("\n3. Check available events:")
        print("   perf list | grep -i unc_m_cas")
        print("   perf list | grep -i llc")
        
        return False
    
    def _test_events(self, read_event, write_event):
        """
        Test if a pair of events works
        Returns True if both events are supported and produce valid output
        """
        try:
            cmd = [
                'perf', 'stat',
                '-e', read_event,
                '-e', write_event,
                '-a', '-x', ',',
                'sleep', '0.1'
            ]
            
            result = subprocess.run(cmd, 
                                  capture_output=True, 
                                  text=True, 
                                  timeout=5)
            
            # Check for errors in stderr
            stderr_lower = result.stderr.lower()
            if 'event syntax error' in stderr_lower:
                return False
            if 'not supported' in stderr_lower:
                return False
            if 'not counted' in stderr_lower:
                return False
            
            # Try to parse the output to verify we got valid data
            has_read = False
            has_write = False
            
            for line in result.stderr.split('\n'):
                if not line.strip():
                    continue
                
                parts = line.split(',')
                if len(parts) < 2:
                    continue
                
                value_str = parts[0].strip()
                event_str = ','.join(parts[1:]).lower()
                
                # Check if we got valid numeric data
                if value_str and value_str[0].isdigit():
                    # Match event names (handle both formats)
                    if read_event.lower().replace('/', '').replace('_', '') in event_str.replace('/', '').replace('_', ''):
                        has_read = True
                    if write_event.lower().replace('/', '').replace('_', '') in event_str.replace('/', '').replace('_', ''):
                        has_write = True
            
            return has_read and has_write
                
        except subprocess.TimeoutExpired:
            return False
        except Exception as e:
            return False
    
    def start_perf_monitor(self):
        """
        Start a single long-running perf process that outputs stats periodically
        Returns the perf process handle
        """
        if not self.events_detected:
            return None
        
        # Convert interval to milliseconds for perf -I flag
        interval_ms = int(self.interval * 1000)
        
        cmd = [
            'perf', 'stat',
            '-e', self.read_event,
            '-e', self.write_event,
            '-a',  # System-wide
            '-I', str(interval_ms),  # Print stats every interval_ms milliseconds
            '-x', ',',  # CSV output
        ]
        
        try:
            # Start perf as a long-running process
            perf_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1  # Line buffered
            )
            print(f"Started continuous perf monitoring (PID: {perf_process.pid})")
            return perf_process
            
        except Exception as e:
            print(f"Error starting perf monitor: {e}")
            return None
    
    def parse_perf_line(self, line):
        """
        Parse a single line of perf output
        Returns (timestamp, event_type, count) where event_type is 'read', 'write', or None
        
        Format: timestamp,count,unit,event,runtime,pct
        Example: 1.001234567,12345,,LLC-loads,1001234567,100.00
        Example: 1.001234567,12345,,unc_m_cas_count.rd,1001234567,100.00
        """
        if not line.strip():
            return None, None, 0
        
        parts = line.split(',')
        if len(parts) < 4:
            return None, None, 0
        
        # parts[0] = timestamp
        # parts[1] = count
        # parts[3] = event name
        
        timestamp_str = parts[0].strip()
        value_str = parts[1].strip()
        event_str = parts[3].lower() if len(parts) > 3 else ''
        
        if not value_str or not value_str[0].isdigit():
            return None, None, 0
        
        try:
            timestamp = float(timestamp_str)
            count = int(value_str)
            
            # Normalize event strings for matching (remove slashes and underscores)
            event_str_normalized = event_str.replace('/', '').replace('_', '')
            read_event_normalized = self.read_event.lower().replace('/', '').replace('_', '')
            write_event_normalized = self.write_event.lower().replace('/', '').replace('_', '')
            
            # Determine if this is read or write event
            if read_event_normalized in event_str_normalized:
                return timestamp, 'read', count
            elif write_event_normalized in event_str_normalized:
                return timestamp, 'write', count
            else:
                return None, None, 0
                
        except ValueError:
            return None, None, 0
    
    def signal_handler(self, signum, frame):
        """Handle Ctrl+C gracefully"""
        print("\nStopping monitoring...")
        self.running = False
        
        if self.perf_process and self.perf_process.poll() is None:
            print("Stopping perf process...")
            self.perf_process.terminate()
            try:
                self.perf_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.perf_process.kill()
        
        if self.command_process and self.command_process.poll() is None:
            print("Terminating command process...")
            self.command_process.terminate()
            try:
                self.command_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                print("Force killing command process...")
                self.command_process.kill()
    
    def run_command(self):
        """Run the specified command and monitor its execution"""
        if isinstance(self.command, str):
            cmd = shlex.split(self.command)
        else:
            cmd = self.command
        
        print(f"Starting command: {' '.join(cmd)}")
        print("-" * 70)
        
        try:
            self.command_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1
            )
            
            # Stream output in real-time
            for line in self.command_process.stdout:
                print(f"[CMD] {line.rstrip()}")
            
            self.command_process.wait()
            self.command_returncode = self.command_process.returncode
            
            print("-" * 70)
            print(f"Command completed with return code: {self.command_returncode}")
            
        except FileNotFoundError:
            print(f"Error: Command not found: {cmd[0]}")
            self.command_returncode = -1
        except Exception as e:
            print(f"Error running command: {e}")
            self.command_returncode = -1
        finally:
            self.running = False
    
    def monitor(self):
        """Main monitoring loop using continuous perf process"""
        signal.signal(signal.SIGINT, self.signal_handler)
        
        # Start command in separate thread if provided
        command_thread = None
        if self.command:
            command_thread = threading.Thread(target=self.run_command)
            command_thread.daemon = True
            command_thread.start()
            time.sleep(0.5)
        
        # Start continuous perf monitoring
        self.perf_process = self.start_perf_monitor()
        if not self.perf_process:
            print("Failed to start perf monitoring")
            return [], [], [], []
        
        print("\nStarting memory bandwidth monitoring...")
        print(f"Interval: {self.interval}s")
        if self.command:
            print("Duration: Until command completes")
        elif self.duration:
            print(f"Duration: {self.duration}s")
        else:
            print("Duration: Press Ctrl+C to stop")
        print("\nTime\t\tRead (GB/s)\tWrite (GB/s)\tTotal (GB/s)")
        print("-" * 70)
        
        start_time = time.time()
        last_timestamp = None
        current_read_count = 0
        current_write_count = 0
        sample_complete = False
        
        # Read from perf stderr in real-time
        while self.running:
            # Check if we've reached duration limit
            if self.duration and (time.time() - start_time) >= self.duration:
                break
            
            # Check if command has finished
            if self.command and command_thread and not command_thread.is_alive():
                print("\nCommand has finished, collecting final samples...")
                time.sleep(self.interval + 0.5)  # Give perf time to output last interval
                break
            
            # Check if perf process died
            if self.perf_process.poll() is not None:
                print("\nPerf process terminated unexpectedly")
                break
            
            # Read lines from perf stderr
            try:
                # Use select to avoid blocking indefinitely
                ready, _, _ = select.select([self.perf_process.stderr], [], [], 0.1)
                
                if ready:
                    line = self.perf_process.stderr.readline()
                    if not line:
                        break
                    
                    timestamp, event_type, count = self.parse_perf_line(line)
                    
                    if timestamp is not None and event_type is not None:
                        # New timestamp means new sample period
                        if last_timestamp is not None and timestamp != last_timestamp:
                            # We have a complete sample, calculate bandwidth
                            if current_read_count > 0 or current_write_count > 0:
                                read_bw = (current_read_count * self.cache_line_size) / self.interval / 1e9
                                write_bw = (current_write_count * self.cache_line_size) / self.interval / 1e9
                                total_bw = read_bw + write_bw
                                
                                current_time = time.time() - start_time
                                self.timestamps.append(current_time)
                                self.read_bandwidth.append(read_bw)
                                self.write_bandwidth.append(write_bw)
                                self.total_bandwidth.append(total_bw)
                                
                                print(f"{current_time:6.1f}s\t\t{read_bw:6.2f}\t\t{write_bw:6.2f}\t\t{total_bw:6.2f}")
                            
                            # Reset counters for new sample
                            current_read_count = 0
                            current_write_count = 0
                        
                        # Accumulate counts for this timestamp
                        if event_type == 'read':
                            current_read_count = count
                        elif event_type == 'write':
                            current_write_count = count
                        
                        last_timestamp = timestamp
                
            except Exception as e:
                print(f"Error reading from perf: {e}")
                break
        
        # Stop perf process
        if self.perf_process and self.perf_process.poll() is None:
            self.perf_process.terminate()
            try:
                self.perf_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.perf_process.kill()
        
        # Wait for command if still running
        if command_thread and command_thread.is_alive():
            print("\nWaiting for command to complete...")
            command_thread.join(timeout=10)
        
        print("\nMonitoring complete!")
        return self.timestamps, self.read_bandwidth, self.write_bandwidth, self.total_bandwidth
    
    def plot(self, output_file='memory_bandwidth.png', show=True):
        """Create a plot of memory bandwidth over time"""
        if len(self.timestamps) == 0:
            print("No data to plot!")
            return
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
        
        # Determine if using uncore or LLC events
        using_uncore = 'unc_' in self.read_event.lower() or 'uncore' in self.read_event.lower()
        
        # Use custom title if provided, otherwise generate one
        if self.title:
            main_title = self.title
        elif self.command:
            cmd_str = self.command if isinstance(self.command, str) else ' '.join(self.command)
            if len(cmd_str) > 50:
                cmd_str = cmd_str[:47] + '...'
            main_title = f'Memory Bandwidth: {cmd_str}'
        else:
            if using_uncore:
                main_title = 'Memory Bandwidth (Uncore DRAM counters)'
            else:
                main_title = 'Memory Bandwidth (LLC-based estimation)'
        
        if using_uncore:
            read_label = 'Read (DRAM)'
            write_label = 'Write (DRAM)'
        else:
            read_label = 'Read (LLC loads)'
            write_label = 'Write (LLC stores)'
        
        # Plot 1: Read and Write bandwidth separately
        ax1.plot(self.timestamps, self.read_bandwidth, 
                label=read_label, linewidth=2, color='blue', marker='o', markersize=3)
        ax1.plot(self.timestamps, self.write_bandwidth, 
                label=write_label, linewidth=2, color='red', marker='s', markersize=3)
        ax1.set_xlabel('Time (seconds)', fontsize=12)
        ax1.set_ylabel('Bandwidth (GB/s)', fontsize=12)
        ax1.set_title(f'{main_title}: Read vs Write', fontsize=14, fontweight='bold')
        ax1.legend(loc='upper right', fontsize=10)
        ax1.grid(True, alpha=0.3)
        
        # Plot 2: Total bandwidth and stacked area
        ax2.fill_between(self.timestamps, 0, self.read_bandwidth, 
                         label='Read', alpha=0.5, color='blue')
        ax2.fill_between(self.timestamps, self.read_bandwidth, self.total_bandwidth, 
                         label='Write', alpha=0.5, color='red')
        ax2.plot(self.timestamps, self.total_bandwidth, 
                label='Total', linewidth=2, color='black', linestyle='--')
        ax2.set_xlabel('Time (seconds)', fontsize=12)
        ax2.set_ylabel('Bandwidth (GB/s)', fontsize=12)
        
        if using_uncore:
            subtitle = 'Total Memory Bandwidth (Stacked) - Accurate DRAM measurement'
        else:
            subtitle = 'Total Memory Bandwidth (Stacked) - LLC estimation'
        
        ax2.set_title(subtitle, fontsize=14, fontweight='bold')
        ax2.legend(loc='upper right', fontsize=10)
        ax2.grid(True, alpha=0.3)
        
        # Add statistics
        stats_text = f"Statistics:\n"
        stats_text += f"Avg Read: {np.mean(self.read_bandwidth):.2f} GB/s\n"
        stats_text += f"Avg Write: {np.mean(self.write_bandwidth):.2f} GB/s\n"
        stats_text += f"Avg Total: {np.mean(self.total_bandwidth):.2f} GB/s\n"
        stats_text += f"Peak Total: {np.max(self.total_bandwidth):.2f} GB/s\n"
        stats_text += f"\nEvents: {self.read_event}\n         {self.write_event}"
        
        if self.command_returncode is not None:
            stats_text += f"\nReturn code: {self.command_returncode}"
        
        ax2.text(0.02, 0.98, stats_text, transform=ax2.transAxes,
                fontsize=9, verticalalignment='top',
                bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
        
        plt.tight_layout()
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        print(f"\nPlot saved to: {output_file}")
        
        if show:
            plt.show()
    
    def save_data(self, output_file='memory_bandwidth.csv'):
        """Save collected data to CSV file"""
        if len(self.timestamps) == 0:
            print("No data to save!")
            return
        
        with open(output_file, 'w') as f:
            f.write("Time(s),Read(GB/s),Write(GB/s),Total(GB/s)\n")
            for t, r, w, total in zip(self.timestamps, self.read_bandwidth, 
                                      self.write_bandwidth, self.total_bandwidth):
                f.write(f"{t:.2f},{r:.4f},{w:.4f},{total:.4f}\n")
        
        print(f"Data saved to: {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description='Monitor and plot memory bandwidth using perf (LLC events)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Monitor for 60 seconds (no command)
  python mem_bandwidth_monitor.py -d 60 -i 1
  
  # Monitor a command (command starts after all options)
  python mem_bandwidth_monitor.py -i 0.5 sleep 10
  python mem_bandwidth_monitor.py -t "Test" ./my_application arg1 arg2
  
  # Use -- to separate script options from command options (recommended)
  python mem_bandwidth_monitor.py -i 0.5 -- mpirun -np 48 lmp -in input.lammps
  python mem_bandwidth_monitor.py -t "Benchmark" -- ./app --option value
  
  # Custom output files and title
  python mem_bandwidth_monitor.py -o lammps_bw.png -c lammps_bw.csv -t "LAMMPS" -- lmp -in input.lammps
  
  # More examples
  python mem_bandwidth_monitor.py -d 30 -t "Baseline" -o baseline.png
  python mem_bandwidth_monitor.py -t "2x Oversubscription" -- ./workload_2x
  
Note: Use -- separator when your command has options starting with - to avoid ambiguity.
      Without --, options are processed until the first non-option argument.
        """
    )
    
    parser.add_argument('-i', '--interval', type=float, default=1.0,
                       help='Sampling interval in seconds (default: 1.0)')
    parser.add_argument('-d', '--duration', type=float, default=None,
                       help='Total monitoring duration in seconds (default: until command completes or Ctrl+C)')
    parser.add_argument('-o', '--output', type=str, default='memory_bandwidth.png',
                       help='Output plot filename (default: memory_bandwidth.png)')
    parser.add_argument('-c', '--csv', type=str, default='memory_bandwidth.csv',
                       help='Output CSV filename (default: memory_bandwidth.csv)')
    parser.add_argument('-t', '--title', type=str, default=None,
                       help='Custom title for the plot (default: auto-generated)')
    parser.add_argument('--no-plot', action='store_true',
                       help='Skip displaying the plot interactively')
    parser.add_argument('command', nargs='*',
                       help='Command and arguments to run (optional)')
    
    args = parser.parse_args()
    
    # Determine command from positional arguments
    command = args.command if args.command else None
    
    # Create monitor
    monitor = MemoryBandwidthMonitor(
        interval=args.interval, 
        duration=args.duration,
        command=command,
        title=args.title
    )
    
    # Check perf availability
    if not monitor.check_perf_availability():
        print("\nTroubleshooting:")
        print("1. Install perf: sudo apt install linux-tools-generic")
        sys.exit(1)
    
    # Detect events
    if not monitor.detect_memory_events():
        sys.exit(1)
    
    # Run monitoring
    monitor.monitor()
    
    # Check command return code
    if monitor.command_returncode is not None and monitor.command_returncode != 0:
        print(f"\nWarning: Command exited with non-zero return code: {monitor.command_returncode}")
    
    # Save and plot
    monitor.save_data(args.csv)
    monitor.plot(args.output, show=not args.no_plot)
    
    # Print statistics
    if len(monitor.timestamps) > 0:
        print("\n" + "="*70)
        print("SUMMARY STATISTICS")
        print("="*70)
        print(f"Total samples:     {len(monitor.timestamps)}")
        print(f"Duration:          {monitor.timestamps[-1]:.2f} seconds")
        print(f"Events used:       {monitor.read_event}, {monitor.write_event}")
        if monitor.command:
            cmd_str = monitor.command if isinstance(monitor.command, str) else ' '.join(monitor.command)
            print(f"Command:           {cmd_str}")
            if monitor.command_returncode is not None:
                print(f"Return code:       {monitor.command_returncode}")
        print(f"\nRead Bandwidth:")
        print(f"  Average:         {np.mean(monitor.read_bandwidth):.2f} GB/s")
        print(f"  Median:          {np.median(monitor.read_bandwidth):.2f} GB/s")
        print(f"  Min:             {np.min(monitor.read_bandwidth):.2f} GB/s")
        print(f"  Max:             {np.max(monitor.read_bandwidth):.2f} GB/s")
        print(f"\nWrite Bandwidth:")
        print(f"  Average:         {np.mean(monitor.write_bandwidth):.2f} GB/s")
        print(f"  Median:          {np.median(monitor.write_bandwidth):.2f} GB/s")
        print(f"  Min:             {np.min(monitor.write_bandwidth):.2f} GB/s")
        print(f"  Max:             {np.max(monitor.write_bandwidth):.2f} GB/s")
        print(f"\nTotal Bandwidth:")
        print(f"  Average:         {np.mean(monitor.total_bandwidth):.2f} GB/s")
        print(f"  Median:          {np.median(monitor.total_bandwidth):.2f} GB/s")
        print(f"  Min:             {np.min(monitor.total_bandwidth):.2f} GB/s")
        print(f"  Max:             {np.max(monitor.total_bandwidth):.2f} GB/s")
        print("="*70)
        
        # Add note about measurement type
        using_uncore = 'unc_' in monitor.read_event.lower() or 'uncore' in monitor.read_event.lower()
        if using_uncore:
            print("\nNote: Using uncore memory controller events (accurate DRAM bandwidth)")
        else:
            print("\nNote: Bandwidth estimated from LLC events (cache misses)")
            print("This correlates with but may not exactly match DRAM bandwidth")
    
    # Exit with command's return code
    if monitor.command_returncode is not None:
        sys.exit(monitor.command_returncode)


if __name__ == '__main__':
    main()
