"""
GreenLedger Carbon Predictor v3.0
Loads trained model and makes predictions.
Supports both old (7 features) and new (11 features) models.
"""
import numpy as np
import joblib, os
import subprocess
import sys
import time
from sklearn.ensemble import RandomForestRegressor

_DIR    = os.path.dirname(os.path.abspath(__file__))
_model  = None
_scaler = None
_features = None

PRACTICE = {"conventional":0,"cover_crop":1,"no_till":2,"agroforestry":3}
SOIL_TYPE = {"black_cotton":3,"alluvial":2,"red_laterite":1,"shallow_rocky":0}

def _load():
    global _model, _scaler, _features
    if _model is None:
        mp = os.path.join(_DIR,"carbon_model.pkl")
        sp = os.path.join(_DIR,"scaler.pkl")
        fp = os.path.join(_DIR,"features.pkl")
        _ensure_artifacts([mp, sp, fp])
        _model  = joblib.load(mp)
        _scaler = joblib.load(sp)
        _features = joblib.load(fp) if os.path.exists(fp) else None

def _ensure_artifacts(paths):
    """Train once on demand when a deployment has no generated model files."""
    if all(os.path.exists(path) for path in paths):
        return

    lock_path = os.path.join(_DIR, ".model_training.lock")
    deadline = time.time() + 300
    while True:
        if all(os.path.exists(path) for path in paths):
            return
        try:
            lock_fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.close(lock_fd)
            break
        except FileExistsError:
            if time.time() >= deadline:
                raise FileNotFoundError("Timed out waiting for carbon model training")
            time.sleep(1)

    try:
        if not all(os.path.exists(path) for path in paths):
            script = os.path.join(_DIR, "train_model.py")
            try:
                subprocess.run([sys.executable, script], cwd=_DIR, check=True)
            except subprocess.CalledProcessError as exc:
                raise FileNotFoundError(
                    "Carbon model training failed; check backend dependencies and logs"
                ) from exc
        if not all(os.path.exists(path) for path in paths):
            raise FileNotFoundError("Training finished without creating all model files")
    finally:
        try:
            os.remove(lock_path)
        except FileNotFoundError:
            pass

def predict(ndvi, area_hectares, soil_organic_carbon,
            temperature_avg=28.0, soil_moisture_avg=55.0,
            monitoring_months=6, practice="conventional",
            practice_years=3, soil_type="black_cotton",
            has_irrigation=1, season_month=8):
    _load()

    prac_enc = float(PRACTICE.get(practice, 0))
    soil_enc = float(SOIL_TYPE.get(soil_type, 3))

    # Build feature vector based on what the model expects
    if _features and len(_features) == 11:
        # New v3 model with 11 features
        X = np.array([[
            float(ndvi), float(area_hectares), float(soil_organic_carbon),
            float(temperature_avg), float(soil_moisture_avg),
            float(monitoring_months), prac_enc,
            float(practice_years), soil_enc,
            float(has_irrigation), float(season_month)
        ]])
    else:
        # Old v2 model with 7 features
        X = np.array([[
            float(ndvi), float(area_hectares), float(soil_organic_carbon),
            float(temperature_avg), float(soil_moisture_avg),
            float(monitoring_months), prac_enc
        ]])

    Xs = _scaler.transform(X)
    # Always use the estimator's public prediction API. Averaging the
    # estimators_ of GradientBoostingRegressor averages residual trees, not
    # complete predictions. Tree spread is only a rough RF disagreement score.
    co2 = float(max(0, _model.predict(Xs)[0]))
    if isinstance(_model, RandomForestRegressor):
        tree_preds = np.array([tree.predict(Xs)[0] for tree in _model.estimators_])
        std = float(np.std(tree_preds))
        conf = max(0.0, round(100 - (std / (co2 + 1e-6)) * 100, 1))
    else:
        std = 0.0
        conf = None

    return {
        "co2_tons":   round(co2, 3),
        "credits":    int(co2),
        "confidence": conf,
        "std_dev":    round(std, 3)
    }
