#!/usr/bin/env python3
"""Ponto de entrada:  python3 simulador/main.py   (ou:  python3 -m simulador)"""

import os
import sys

# Permite rodar como script, de qualquer pasta
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from simulador.app import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
