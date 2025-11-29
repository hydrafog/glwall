#!/usr/bin/env python3
import os
import re
import sys

def remove_comments(text):
    def replacer(match):
        s = match.group(0)
        if s.startswith('/'):
            return " " # replace comment with a space to avoid joining tokens
        else:
            return s
    
    # Regex to match C-style comments (// and /* */) and string literals
    # We match strings to avoid removing comments inside strings.
    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])*\'|"(?:\\.|[^\\"])*"',
        re.DOTALL | re.MULTILINE
    )
    return re.sub(pattern, replacer, text)

def process_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        new_content = remove_comments(content)
        
        if content != new_content:
            print(f"Stripping comments from: {filepath}")
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)
    except Exception as e:
        print(f"Error processing {filepath}: {e}")

def main():
    extensions = {'.c', '.h', '.frag', '.vert', '.glsl'}
    target_dirs = ['src', 'shaders'] # Adjust based on project structure
    
    # If arguments provided, use them as target directories/files
    if len(sys.argv) > 1:
        paths = sys.argv[1:]
    else:
        paths = target_dirs

    for path in paths:
        if os.path.isfile(path):
             if os.path.splitext(path)[1] in extensions:
                 process_file(path)
        else:
            for root, _, files in os.walk(path):
                for file in files:
                    if os.path.splitext(file)[1] in extensions:
                        process_file(os.path.join(root, file))

if __name__ == "__main__":
    main()
