# Visualization Improvements Summary

## Changes Made

### 1. **Drift Markers for burn_in=0 Datasets**
Previously, datasets with `burn_in=0` (drift starting from the beginning) had no visual drift markers because the code only drew markers when `burn_in > 0`.

**Fixed:** Now all drift types show start markers even when drift occurs from timestep 0:
- **Abrupt drift:** Red dashed vertical line at t=0 labeled "Drift at Start"
- **Gradual drift:** Orange dashed line at t=0 with gradient shading showing the entire drift transition phase
- **Sudden drift:** Series of orange markers from t=0
- **Recurrent drift:** Purple markers from t=0 showing cycle points

### 2. **Gradient Grayscale Visualization for Gradual Drift**
Replaced solid orange background with gradient shading to show the **phase transition** during gradual drift.

**Implementation:**
- Divides drift window into 20 segments (for distance plots) or 15 segments (for other plots)
- Each segment has progressively darker alpha transparency (from 0.05 to 0.30)
- Creates visual "fade-in" effect showing how drift intensity increases over time
- Both drift start and drift end are marked with vertical lines:
  - **Drift Start:** Dark orange dashed line (`--`)
  - **Drift End:** Dark red dashed line (`--`)

**Applies to 4 visualization panels:**
1. Distribution Divergence (Wasserstein/Hellinger distances)
2. Centroid Drift (||μ_t - μ_ref||)
3. PCA Feature Space (subtle yellow gradient background)
4. Label Distribution Over Time

### 3. **Bug Fix: Config Loading**
Fixed filter that was incorrectly excluding 6 configs containing `_comment` keys.

**Before:**
```python
return [c for c in configs if not c.get('name', '').startswith('_') and '_comment' not in c]
```

**After:**
```python
return [c for c in configs if not c.get('name', '').startswith('_')]
```

**Result:** All 20 datasets now generate visualizations (was 14 before)

## Gradual Drift Datasets with Gradient Visualization

The following 6 datasets now show gradient phase drift markers:

1. **linear_gradual_rotation** (burn_in=0, drift_width=10000)
   - Full-length gradual transition from start to end
   
2. **cake_gradual_rotation** (burn_in=0, drift_width=10000)
   - Circular boundary with gradual rotation throughout
   
3. **chocolate_gradual_rotation** (burn_in=0, drift_width=10000)
   - Grid boundary with gradual transformation
   
4. **torus_gradual_rolling** (burn_in=0, drift_width=10000)
   - Rolling torus with smooth transition phase
   
5. **sensor_drift_gradual** (burn_in=1000, drift_width=4000)
   - High-dimensional hash-based with mid-stream drift
   
6. **cake_multiclass_gradual** (burn_in=0, drift_width=10000)
   - 5-class circular boundary with gradual drift

## Visual Examples

### Gradual Drift with Gradient Shading
```
Distribution Divergence
│
│  ┌─────────────────────────── Drift End (dark red --)
│  │  Gradient shading:
│  │  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ (alpha: 0.05 → 0.30)
│  └─────────────────────────── Drift Start (dark orange --)
│
└─────────────────────────────> Time
```

### Abrupt Drift at Start (burn_in=0)
```
│
│  │
│  │ ← Red dashed line at t=0
│  │    "Drift at Start"
│
└─────────────────────────────> Time
```

## Comparison: Before vs After

| Feature | Before | After |
|---------|--------|-------|
| burn_in=0 marker | ❌ No marker | ✅ Marked at t=0 |
| Gradual drift visualization | Solid orange rectangle | Gradient with 15-20 steps |
| Drift end marker | ❌ Not shown | ✅ Dark red line |
| Configs loaded | 14 / 20 | 20 / 20 ✅ |
| Visual clarity | Abrupt/sudden look same | Each type distinct |

## Technical Details

### Gradient Algorithm
```python
n_segments = 20  # Fine-grained gradient
segment_width = (drift_end - burn_in) / n_segments
for i in range(n_segments):
    segment_start = burn_in + i * segment_width
    segment_end = segment_start + segment_width
    alpha = 0.05 + (i / n_segments) * 0.25  # Linear ramp
    ax.axvspan(segment_start, segment_end, color='orange', 
               alpha=alpha, linewidth=0)
```

### Benefits
- **Intuitive:** Darker regions = more drift has occurred
- **Precise:** 20 segments provide smooth visual transition
- **Distinct:** Easy to distinguish gradual from abrupt/sudden drift
- **Informative:** Shows both start time and duration of drift

## Files Modified
- `visualize_drift.py`:
  - Lines 11-16: Fixed `load_config()` filter
  - Lines 83-125: Updated `plot_distance_over_time()` with gradient + burn_in=0 support
  - Lines 138-152: Updated `plot_centroid_drift()` with gradient
  - Lines 207-213: Updated `plot_pca_projection()` with subtle gradient
  - Lines 239-257: Updated `plot_label_distribution_over_time()` with gradient

## Output
All 20 visualizations generated successfully in:
```
drift_concept_visualize/
├── linear_gradual_rotation_combined_analysis.png
├── cake_gradual_rotation_combined_analysis.png
├── chocolate_gradual_rotation_combined_analysis.png
├── torus_gradual_rolling_combined_analysis.png
├── sensor_drift_gradual_combined_analysis.png
├── cake_multiclass_gradual_combined_analysis.png
└── ... (14 other drift types)
```

## Usage
```bash
cd tools/ConceptDriftGenerator
python3 visualize_drift.py
```

The script automatically:
1. Loads all 20 configurations from `parameters.json`
2. Matches dataset files with flexible naming patterns
3. Generates 4-panel visualizations with appropriate drift markers
4. Uses gradient shading for gradual drift types
5. Shows drift-at-start markers for burn_in=0 datasets
