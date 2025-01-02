import os

def prepend_filename_to_files(base_directories, file_extensions=('.cpp', '.hpp')):
    for base_dir in base_directories:
        for root, _, files in os.walk(base_dir):
            for file in files:
                if file.endswith(file_extensions):
                    file_path = os.path.join(root, file)
                    with open(file_path, 'r+', encoding='utf-8') as f:
                        content = f.read()
                        # Check if the filename is already at the top
                        if content.startswith(f"// File: {file}\n"):
                            print(f"Skipped (already added): {file_path}")
                            continue
                        f.seek(0, 0)
                        f.write(f"// File: {file}\n{content}")
                        print(f"Prepended filename to: {file_path}")

if __name__ == "__main__":
    # Specify the base directories
    base_dirs = ["src", "include"]
    prepend_filename_to_files(base_dirs)
