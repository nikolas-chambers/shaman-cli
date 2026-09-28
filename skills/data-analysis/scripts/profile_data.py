#!/usr/bin/env python3
"""Profile a CSV/TSV/Excel/JSON/Parquet file: shape, types, missing, distinct, stats, samples.

  profile_data.py data.csv [--sheet NAME] [--rows 5]
"""
import argparse
import sys

try:
    import pandas as pd
except ImportError:
    sys.exit("missing Python module 'pandas': python3 -m pip install pandas")


def load(path, sheet):
    lower = path.lower()
    if lower.endswith((".xlsx", ".xlsm", ".xls")):
        return pd.read_excel(path, sheet_name=sheet or 0)
    if lower.endswith(".json"):
        return pd.read_json(path)
    if lower.endswith(".parquet"):
        return pd.read_parquet(path)
    return pd.read_csv(path, sep=None, engine="python")  # sniff , ; \t |


def main():
    p = argparse.ArgumentParser()
    p.add_argument("path")
    p.add_argument("--sheet")
    p.add_argument("--rows", type=int, default=5)
    a = p.parse_args()
    df = load(a.path, a.sheet)
    print(f"shape: {df.shape[0]} rows x {df.shape[1]} columns\n")
    summary = pd.DataFrame({
        "dtype": df.dtypes.astype(str),
        "missing": df.isna().sum(),
        "missing_%": (df.isna().mean() * 100).round(1),
        "distinct": df.nunique(),
    })
    print(summary.to_string(), "\n")
    numeric = df.select_dtypes("number")
    if not numeric.empty:
        print(numeric.describe().T.round(3).to_string(), "\n")
    for col in df.select_dtypes(exclude="number").columns[:10]:
        top = df[col].value_counts().head(5)
        print(f"top values of {col!r}: " + ", ".join(f"{k} ({v})" for k, v in top.items()))
    print(f"\nduplicate rows: {df.duplicated().sum()}")
    print(f"\nfirst {a.rows} rows:\n{df.head(a.rows).to_string()}")


if __name__ == "__main__":
    main()
