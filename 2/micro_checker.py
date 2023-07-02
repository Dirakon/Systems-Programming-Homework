import argparse
import subprocess
import sys

parser = argparse.ArgumentParser(description='Tests for shell')
parser.add_argument('-e', type=str, default='./a.out',
                    help='executable shell file')
parser.add_argument('-r', type=str, help='result file')
parser.add_argument('-t', action='store_true', default=False,
                    help='run without checks')
parser.add_argument('--max', type=int, choices=[15, 20, 25], default=15,
                    help='max points number')
args = parser.parse_args()

lines = [
    'echo 1',
    "printf \"import time\\n\\\n" \
    "time.sleep(0.1)\\n\\\n" \
    "f = open('test.txt', 'w')\\n\\\n" \
    "f.write('Text\\\\\\n')\\n\\\n" \
    "f.close()\\n\" > test.py",
    "python test.py | exit 0",
    "cat test.txt",
    'echo next thing'

    # "time.sleep(0.1)\\n\\\n" \
    # "f = open('test.txt', 'w')\\n\\\n" \
    # "f.write('Text\\\\\\n')\\n\\\n" \
    # "f.close()\\n\" > test.py",
    # "python test.py | exit 0",
    # "cat test.txt",
    # 'echo next thing'
]

prefix = '--------------------------------'


def finish(code):
    sys.exit(code)


def open_new_shell():
    return subprocess.Popen([args.e], shell=False, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, bufsize=0)


def exit_failure():
    print('{}\nThe tests did not pass'.format(prefix))
    finish(-1)


command = ''
for test_i, test in enumerate(lines, 1):
    command += 'echo "$> Test {}"\n'.format(test_i)
    command += '{}\n'.format(test)

p = open_new_shell()
try:
    output = p.communicate(command.encode(), 30)[0].decode()
except subprocess.TimeoutExpired:
    print('Too long no output. Probably you forgot to process EOF')
    finish(-1)
if p.returncode != 0:
    print('Expected zero exit code')
    finish(-1)

print(output)

p.terminate()
