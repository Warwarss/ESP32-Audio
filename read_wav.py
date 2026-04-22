import wave
import struct
import matplotlib.pyplot as plt
import numpy as np
import os
import argparse

def read_wav_file(file_path, num_samples=1000):
    with wave.open(file_path, 'rb') as wav_file:
        metadata = wav_file.getparams()
        frames = wav_file.readframes(metadata.nframes)
        print(metadata)

        samples = []
        n_channels = metadata.nchannels
        sampwidth = metadata.sampwidth
        n_frames = metadata.nframes

        if sampwidth == 1:  # 8-bit unsigned PCM
            fmt = f"<{len(frames)}B"
            unpacked_samples = struct.unpack(fmt, frames)
            # Convert unsigned 8-bit (0-255) to signed range (-128 to 127)
            samples = [(s - 128) for s in unpacked_samples]
        elif sampwidth == 2:  # 16-bit signed PCM
            fmt = f"<{len(frames) // 2}h"
            samples = list(struct.unpack(fmt, frames))
        elif sampwidth == 3:  # 24-bit signed PCM
            fmt = f"<{len(frames) // 3 * 3}B" # Read all bytes
            all_bytes = struct.unpack(fmt, frames)
            samples = []
            for i in range(0, len(all_bytes), 3):
                # Combine 3 bytes into a 24-bit integer (little-endian)
                b1, b2, b3 = all_bytes[i], all_bytes[i+1], all_bytes[i+2]
                sample_val = b1 | (b2 << 8) | (b3 << 16)
                # Handle sign extension for negative values
                if sample_val >= (1 << 23):  # Check if the sign bit (bit 23) is set
                    sample_val -= (1 << 24)  # Convert to negative value
                samples.append(sample_val)
        elif sampwidth == 4: # 32-bit signed PCM (assuming integer format)
             fmt = f"<{len(frames) // 4}i"
             samples = list(struct.unpack(fmt, frames))
        else:
            print(f"Unsupported sample width: {sampwidth}")
            return

        # If stereo, deinterleave (optional, depends on what you need)
        # if n_channels == 2:
        #     left_channel = samples[0::2]
        #     right_channel = samples[1::2]
        #     print("Left channel samples:", left_channel[:50])
        #     print("Right channel samples:", right_channel[:50])
        # else:
        #     print("Mono samples:", samples[:100])

        print("Samples (interleaved if stereo):", samples[0], samples[1], samples[2], samples[3], samples[4], samples[5], samples[6], samples[7], samples[8], samples[9])
        
        # Plot the specified number of samples
        plot_samples(samples, metadata, num_samples=num_samples)
        
        return samples


def plot_samples(samples, metadata, num_samples=1000):
    """Plot the first num_samples of the WAV file"""
    # Limit to available samples
    plot_count = min(num_samples, len(samples))
    samples_to_plot = samples[:plot_count]
    
    # Create time axis
    sample_rate = metadata.framerate
    time_axis = np.arange(plot_count)
    
    # Create the plot
    plt.figure(figsize=(12, 6))
    
    if metadata.nchannels == 1:
        # Mono audio
        plt.plot(time_axis, samples_to_plot, 'b-', linewidth=0.5)
        plt.title(f'WAV File - First {plot_count} Samples (Mono)\n'
                 f'Sample Rate: {sample_rate} Hz, Bit Depth: {metadata.sampwidth * 8}-bit')
        plt.ylabel('Amplitude')
    else:
        # Stereo audio - deinterleave and plot both channels
        left_channel = samples_to_plot[0::2]
        right_channel = samples_to_plot[1::2]
        
        # Adjust time axis for stereo (half the samples per channel)
        stereo_time_axis = np.arange(len(left_channel))
        
        plt.subplot(2, 1, 1)
        plt.plot(stereo_time_axis, left_channel, 'b-', linewidth=0.5)
        plt.title(f'WAV File - First {plot_count} Samples (Stereo)\n'
                 f'Sample Rate: {sample_rate} Hz, Bit Depth: {metadata.sampwidth * 8}-bit')
        plt.ylabel('Left Channel')
        plt.grid(True, alpha=0.3)
        
        plt.subplot(2, 1, 2)
        plt.plot(stereo_time_axis, right_channel, 'r-', linewidth=0.5)
        plt.ylabel('Right Channel')
        plt.grid(True, alpha=0.3)
    
    plt.xlabel('Samples')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    # Add some statistics
    max_val = max(samples_to_plot)
    min_val = min(samples_to_plot)
    avg_val = np.mean(samples_to_plot)
    
    print(f"\nPlot Statistics (first {plot_count} samples):")
    print(f"Max amplitude: {max_val}")
    print(f"Min amplitude: {min_val}")
    print(f"Average amplitude: {avg_val:.2f}")
    print(f"Dynamic range: {max_val - min_val}")
    
    plt.show()


def list_wav_files(directory):
    """List all WAV files in the given directory"""
    wav_files = []
    try:
        for file in os.listdir(directory):
            if file.lower().endswith('.wav'):
                wav_files.append(file)
    except FileNotFoundError:
        print(f"Directory not found: {directory}")
        return []
    
    return sorted(wav_files)


def choose_file_interactive(directory):
    """Interactive file selection"""
    wav_files = list_wav_files(directory)
    
    if not wav_files:
        print(f"No WAV files found in {directory}")
        return None
    
    print(f"\nFound {len(wav_files)} WAV file(s) in {directory}:")
    print("-" * 50)
    
    for i, file in enumerate(wav_files, 1):
        file_path = os.path.join(directory, file)
        try:
            file_size = os.path.getsize(file_path)
            print(f"{i:2}. {file} ({file_size:,} bytes)")
        except:
            print(f"{i:2}. {file}")
    
    while True:
        try:
            choice = input(f"\nSelect a file (1-{len(wav_files)}): ").strip()
            if choice.lower() in ['q', 'quit', 'exit']:
                return None
            
            file_index = int(choice) - 1
            if 0 <= file_index < len(wav_files):
                return os.path.join(directory, wav_files[file_index])
            else:
                print(f"Please enter a number between 1 and {len(wav_files)}")
        except ValueError:
            print("Please enter a valid number (or 'q' to quit)")


def get_sample_count():
    """Get the number of samples to plot from user"""
    while True:
        try:
            samples = input("\nEnter number of samples to plot (default: 1000): ").strip()
            if not samples:
                return 1000
            
            sample_count = int(samples)
            if sample_count > 0:
                return sample_count
            else:
                print("Please enter a positive number")
        except ValueError:
            print("Please enter a valid number")


def main():
    """Main function with command line interface"""
    parser = argparse.ArgumentParser(description='WAV File Reader and Plotter')
    parser.add_argument('-f', '--file', type=str, help='Specific WAV file to read')
    parser.add_argument('-n', '--samples', type=int, default=1000, help='Number of samples to plot (default: 1000)')
    parser.add_argument('-d', '--directory', type=str, 
                       default='C:/Users/ACER/Documents/PlatformIO/Projects/Demo/data/',
                       help='Directory to search for WAV files')
    parser.add_argument('-i', '--interactive', action='store_true', 
                       help='Interactive mode for file selection')
    
    args = parser.parse_args()
    
    # Ensure directory path ends with separator
    if not args.directory.endswith('/') and not args.directory.endswith('\\'):
        args.directory += '/'
    
    if args.interactive or not args.file:
        # Interactive mode
        print("WAV File Reader and Plotter - Interactive Mode")
        print("=" * 50)
        
        selected_file = choose_file_interactive(args.directory)
        if not selected_file:
            print("No file selected. Exiting.")
            return
        
        sample_count = get_sample_count()
        
    else:
        # Command line mode
        if os.path.isabs(args.file):
            selected_file = args.file
        else:
            selected_file = os.path.join(args.directory, args.file)
        
        sample_count = args.samples
        
        if not os.path.exists(selected_file):
            print(f"File not found: {selected_file}")
            return
    
    print(f"\nReading WAV file: {selected_file}")
    print(f"Plotting first {sample_count} samples...")
    print("-" * 50)
    
    try:
        samples = read_wav_file(selected_file, sample_count)
        if samples:
            print(f"\nSuccessfully processed {len(samples)} samples from {os.path.basename(selected_file)}")
    except Exception as e:
        print(f"Error reading file: {e}")


if __name__ == "__main__":
    main()