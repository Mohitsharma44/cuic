import os
import sys

current_path = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.join(current_path, os.path.abspath('..')))

print(current_path)