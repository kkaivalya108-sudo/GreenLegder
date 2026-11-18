"""Small, database-free Flask app for the ESP32 carbon estimate endpoint."""
from flask import Flask, jsonify, request
from flask_cors import CORS
import os

from model.predictor import predict

app = Flask(__name__)
CORS(app, origins="*")

CROP_FACTORS = {
    "sugarcane": 1.45, "soybean": 1.20, "wheat": 1.10,
    "cotton": 1.00, "jowar": 0.95, "vegetables": 0.65,
    "grapes": 1.05, "orange": 1.10, "pomegranate": 1.00,
    "other": 0.90,
}


@app.route("/api/health", methods=["GET"])
def health():
    return jsonify({"api": "carbon-model", "status": "ok"})


@app.route("/api/ai/estimate", methods=["POST"])
def estimate():
    data = request.get_json(force=True)
    for field in ("ndvi", "area_hectares", "soil_organic_carbon"):
        if field not in data:
            return jsonify({"error": f"Missing: {field}"}), 400

    try:
        result = predict(
            ndvi=float(data["ndvi"]),
            area_hectares=float(data["area_hectares"]),
            soil_organic_carbon=float(data["soil_organic_carbon"]),
            temperature_avg=float(data.get("temperature_avg", 28.0)),
            soil_moisture_avg=float(data.get("soil_moisture_avg", 55.0)),
            monitoring_months=int(data.get("monitoring_months", 6)),
            practice=data.get("practice", "conventional"),
        )
    except FileNotFoundError as exc:
        return jsonify({"error": str(exc)}), 503
    except (TypeError, ValueError) as exc:
        return jsonify({"error": f"Invalid model input: {exc}"}), 400

    crop = str(data.get("crop_type", "other")).lower()
    factor = CROP_FACTORS.get(crop, 0.90)
    result["co2_tons"] = round(result["co2_tons"] * factor, 3)
    result["credits"] = int(result["co2_tons"])
    result["crop_factor"] = factor
    return jsonify(result)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(os.getenv("AI_PORT", "5001")), debug=False)
