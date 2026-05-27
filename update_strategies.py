#!/usr/bin/env python3
import os
import re

def get_class_name(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    match = re.search(r'class\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z0-9_]+)', content)
    if match:
        return match.group(1)
    return None

def to_snake_case(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    res = re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()
    return res

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    strat_dir = os.path.join(base_dir, 'faststrategy')
    
    if not os.path.exists(strat_dir):
        return
        
    strat_files = []
    for f in sorted(os.listdir(strat_dir)):
        if f.endswith('.hpp') or f.endswith('.h'):
            class_name = get_class_name(os.path.join(strat_dir, f))
            if class_name:
                strat_files.append((f, class_name))
                
    # 1. Update strategyinclude.hpp
    inc_path = os.path.join(base_dir, 'strategyinclude.hpp')
    with open(inc_path, 'w') as f:
        f.write("#pragma once\n")
        for filename, _ in strat_files:
            f.write(f'#include "./faststrategy/{filename}"\n')
            
    # 2. Update strategyPool.hpp
    pool_path = os.path.join(base_dir, 'strategyPool.hpp')
    with open(pool_path, 'r') as f:
        pool_content = f.read()
        
    pools_str = "// POOLS_START\n"
    for _, class_name in strat_files:
        var_name = class_name[0].lower() + class_name[1:] + "Pool"
        if class_name == "BASIC":
            var_name = "basicPool"
        elif class_name == "BASIC2":
            var_name = "basicPool2"
        elif class_name == "AGAIN":
            var_name = "againPool"
        pools_str += f"static StrategyPool<{class_name}, HFT::MAXHFTSYMBOL> {var_name};\n"
    pools_str += "// POOLS_END"
    
    pool_content = re.sub(r'// POOLS_START.*?// POOLS_END', pools_str, pool_content, flags=re.DOTALL)
    with open(pool_path, 'w') as f:
        f.write(pool_content)
        
    # 3. Update strategyHandler.hpp
    handler_path = os.path.join(base_dir, 'strategyHandler.hpp')
    with open(handler_path, 'r') as f:
        handler_content = f.read()
        
    regs_str = "  // REGISTRATIONS_START\n"
    for _, class_name in strat_files:
        var_name = class_name[0].lower() + class_name[1:] + "Pool"
        if class_name == "BASIC":
            var_name = "basicPool"
        elif class_name == "BASIC2":
            var_name = "basicPool2"
        elif class_name == "AGAIN":
            var_name = "againPool"
        reg_name = to_snake_case(class_name)
        regs_str += f'  registerStrategy<{class_name}, HFT::MAXHFTSYMBOL>(registry, "{reg_name}", {var_name});\n'
    regs_str += "  // REGISTRATIONS_END"
    
    handler_content = re.sub(r'// REGISTRATIONS_START.*?// REGISTRATIONS_END', regs_str, handler_content, flags=re.DOTALL)
    with open(handler_path, 'w') as f:
        f.write(handler_content)

if __name__ == '__main__':
    main()
