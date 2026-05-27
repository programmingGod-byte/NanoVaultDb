#!/usr/bin/env bash
python3 update_strategies.py
rm -rf dist/* build/* wheelhouse/* nanovaultdb.egg-info
for PYTHON_CMD in python3.10 python3.11 python3.12; do
    if command -v $PYTHON_CMD >/dev/null 2>&1; then
        echo "Creating isolated virtual environment for $PYTHON_CMD to prevent OS package pollution..."
        $PYTHON_CMD -m venv .venv_$PYTHON_CMD
        ./.venv_$PYTHON_CMD/bin/pip install --upgrade pip setuptools wheel pybind11
        CC=gcc-13 CXX=g++-13 ./.venv_$PYTHON_CMD/bin/python setup.py bdist_wheel
        rm -rf .venv_$PYTHON_CMD
    else
        echo "Interpreter $PYTHON_CMD not found on your system, skipping."
    fi
done
auditwheel repair dist/*.whl
echo "Successfully built and repaired Python wheels!"
