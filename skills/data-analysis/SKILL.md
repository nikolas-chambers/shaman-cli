---
name: data-analysis
description: Explore, clean, analyse and chart tabular data (CSV, Excel, JSON, SQL results) with pandas, and report findings with the numbers behind them. Use for questions about datasets, metrics or trends.
---
# Data analysis

1. **Profile first**: `scripts/profile_data.py data.csv` prints shape, column types, missing values, distinct counts, numeric summaries and sample rows. Read it before analysing.
2. **Clarify the question**: the metric, the grouping, the time window, and what decision it informs.
3. **Clean deliberately**: parse dates and numbers explicitly; handle missing values and duplicates on purpose and say what you did; check units and currencies; watch for outliers that are really data errors.
4. **Analyse in a script** (`analysis.py`), not by eyeballing: groupby/aggregate, pivot, joins with explicit keys and row-count checks after each join.
5. **Chart when it helps**: matplotlib/seaborn to PNG; one message per chart; labelled axes with units; start bar charts at zero; readable in greyscale.
6. **Report**: the answer first, then the key numbers, then caveats (sample size, missing data, assumptions). Include the script so the result is reproducible.

Large files: read in chunks (`chunksize`), select columns (`usecols`), or use DuckDB/SQL over the file.
