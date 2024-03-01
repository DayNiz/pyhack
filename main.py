import random
import platform
import os
import time
import sys

def clearScreen():
    if platform.system() == "Windows":
        os.system("cls")
    elif platform.system() == "Linux" or "Darwin":
        os.system("clear")

def delay_print(s):
    for c in s:
        sys.stdout.write(c)
        sys.stdout.flush()
        time.sleep(0.03)

clearScreen()
delay_print("Press F11 for a better experience...")
input()
delay_print("Ready?\n3\n2\n1\nHere we go!!")

with open("testusb.c") as f:
    clearScreen()
    lines = f.readlines()
    while True:
        delay_print(random.choice(lines))
