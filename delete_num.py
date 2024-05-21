import sys
import os 
import tempfile 
import sys

def remove_digit_prefix(line):
    "检查一行是否以数字开头，如果是，则返回删除数字后的内容"
    index = 0
    while index < len(line) and line[index].isdigit():
        index += 1
    return line[index:].lstrip()

def process_file(input_file):
    # 创建一个临时文件
    tmp_fd, tmp_path = tempfile.mkstemp()
    try:
        with open(input_file, 'r', encoding='utf-8') as infile, open(tmp_path, 'w', encoding='utf-8') as outfile:
            for line in infile:
                raw = line
                line = line.strip()  # 去掉行首空格，行尾的换行符
                if line.startswith(tuple(str(i) for i in range(10))):  # 检查是否以数字开头
                    line = remove_digit_prefix(line)
                    outfile.write(line + '\n')  # 重新插入换行符
                else:
                    outfile.write(raw)
        # 将临时文件的内容覆盖原文件
        os.replace(tmp_path, input_file)
    finally:
        # 关闭临时文件描述符
        os.close(tmp_fd)

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('Usage: delete_num.py <input_file>')
        sys.exit(1)
    else:
        process_file(sys.argv[1])