import os
import sys

def concatenate_to_stdout(base_directories, file_extensions=('.cpp', '.hpp')):
    for base_dir in base_directories:
        for root, _, files in os.walk(base_dir):
            for file in files:
                if file.endswith(file_extensions):
                    file_path = os.path.join(root, file)
                    try:
                        with open(file_path, 'r', encoding='utf-8') as infile:
                            sys.stdout.write(f"// Start of {file}\n")
                            sys.stdout.write(infile.read())
                            sys.stdout.write(f"\n// End of {file}\n\n")
                    except Exception as e:
                        sys.stderr.write(f"Error reading {file_path}: {e}\n")

if __name__ == "__main__":
    # Define directories to scan
    base_dirs = ["src", "include"]

    # Concatenate files to stdout
    concatenate_to_stdout(base_dirs)
