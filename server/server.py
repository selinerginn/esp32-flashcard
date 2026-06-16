"""
ESP32 Flashcard Server
Çalıştır: python server.py
Arayüz:   http://localhost:5000
"""

from flask import Flask, jsonify, request, render_template
import json, os
from datetime import datetime

app = Flask(__name__)
DATA_FILE = "decks.json"

# ════════════════════════════════════════════════════════
#  LİMİT DEĞİŞKENLERİ
# ════════════════════════════════════════════════════════
MAX_DECKS = 10        # En fazla oluşturulabilecek deste sayısı
MAX_CARDS = 50         # Her bir destede olabilecek en fazla kart sayısı
TOTAL_MAX_CARDS = 50  # TÜM destelerdeki genel maksimum kart sayısı

# ════════════════════════════════════════════════════════
#  VERİ KATMANI
# ════════════════════════════════════════════════════════

def load_data():
    if not os.path.exists(DATA_FILE):
        return {"decks": [], "next_deck_id": 1, "next_card_id": 1}
    with open(DATA_FILE, "r", encoding="utf-8") as f:
        return json.load(f)

def save_data(data):
    with open(DATA_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

def find_deck(data, deck_id):
    return next((d for d in data["decks"] if d["id"] == deck_id), None)

# Tüm destelerdeki toplam kart sayısını hesaplar
def get_total_card_count(data):
    return sum(len(deck["cards"]) for deck in data["decks"])

# ════════════════════════════════════════════════════════
#  ESP32 API
# ════════════════════════════════════════════════════════

@app.route("/all")
def api_all():
    data = load_data()
    return jsonify({"decks": data["decks"]})

# ════════════════════════════════════════════════════════
#  WEB API
# ════════════════════════════════════════════════════════

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/api/decks", methods=["GET"])
def web_get_decks():
    data = load_data()
    return jsonify({
        "decks": [
            {"id": d["id"], "name": d["name"],
             "card_count": len(d["cards"]),
             "updated_at": d.get("updated_at", "")}
            for d in data["decks"]
        ]
    })

@app.route("/api/decks", methods=["POST"])
def web_create_deck():
    data = load_data()
    
    # Deste Limit Kontrolü
    if len(data["decks"]) >= MAX_DECKS:
        return jsonify({"error": f"Limit exceeded: You can add a maximum of {MAX_DECKS} decks."}), 400
        
    body = request.get_json()
    deck = {
        "id": data["next_deck_id"],
        "name": body["name"],
        "cards": [],
        "updated_at": datetime.now().strftime("%d.%m.%Y %H:%M")
    }
    data["decks"].append(deck)
    data["next_deck_id"] += 1
    save_data(data)
    return jsonify({"id": deck["id"], "name": deck["name"]})

@app.route("/api/deck/<int:deck_id>", methods=["GET"])
def web_get_deck(deck_id):
    data = load_data()
    deck = find_deck(data, deck_id)
    if not deck:
        return jsonify({"error": "Not found"}), 404
    return jsonify(deck)

@app.route("/api/deck/<int:deck_id>", methods=["PUT"])
def web_update_deck(deck_id):
    data = load_data()
    deck = find_deck(data, deck_id)
    if not deck:
        return jsonify({"error": "Not found"}), 404
    body = request.get_json()
    deck["name"] = body.get("name", deck["name"])
    deck["updated_at"] = datetime.now().strftime("%d.%m.%Y %H:%M")
    save_data(data)
    return jsonify({"ok": True})

@app.route("/api/deck/<int:deck_id>", methods=["DELETE"])
def web_delete_deck(deck_id):
    data = load_data()
    data["decks"] = [d for d in data["decks"] if d["id"] != deck_id]
    save_data(data)
    return jsonify({"ok": True})

@app.route("/api/deck/<int:deck_id>/cards", methods=["POST"])
def web_add_card(deck_id):
    data = load_data()
    deck = find_deck(data, deck_id)
    if not deck:
        return jsonify({"error": "Not found"}), 404
        
    # 1. Kontrol: Deste içi limit
    if len(deck["cards"]) >= MAX_CARDS:
        return jsonify({"error": f"Limit exceeded: A maximum of {MAX_CARDS} cards can be added to this deck."}), 400
        
    # 2. Kontrol: Genel (Tüm desteler) limit
    if get_total_card_count(data) >= TOTAL_MAX_CARDS:
        return jsonify({"error": f"The total limit has been exceeded: The system can have a maximum of {TOTAL_MAX_CARDS} cards in total."}), 400
        
    body = request.get_json()
    card = {"id": data["next_card_id"],
            "question": body["question"],
            "answer": body["answer"]}
    deck["cards"].append(card)
    deck["updated_at"] = datetime.now().strftime("%d.%m.%Y %H:%M")
    data["next_card_id"] += 1
    save_data(data)
    return jsonify({"id": card["id"]})

@app.route("/api/deck/<int:deck_id>/import", methods=["POST"])
def web_import_cards(deck_id):
    data = load_data()
    deck = find_deck(data, deck_id)
    if not deck:
        return jsonify({"error": "Not found"}), 404
        
    body = request.get_json()
    cards_to_import = body.get("cards", [])
    
    # 1. Kontrol: Deste içi limit
    if len(deck["cards"]) + len(cards_to_import) > MAX_CARDS:
        return jsonify({"error": f"Limit exceeded: Maximum {MAX_CARDS} cards allowed in the deck. Import cancelled."}), 400
        
    # 2. Kontrol: Genel (Tüm desteler) limit
    if get_total_card_count(data) + len(cards_to_import) > TOTAL_MAX_CARDS:
        return jsonify({"error": f"General limit exceeded: Maximum {TOTAL_MAX_CARDS} cards allowed in the system. Import cancelled."}), 400
        
    count = 0
    for c in cards_to_import:
        card = {"id": data["next_card_id"],
                "question": c["question"],
                "answer": c["answer"]}
        deck["cards"].append(card)
        data["next_card_id"] += 1
        count += 1
    deck["updated_at"] = datetime.now().strftime("%d.%m.%Y %H:%M")
    save_data(data)
    return jsonify({"imported": count})

@app.route("/api/card/<int:card_id>", methods=["PUT"])
def web_update_card(card_id):
    data = load_data()
    body = request.get_json()
    for deck in data["decks"]:
        for card in deck["cards"]:
            if card["id"] == card_id:
                card["question"] = body.get("question", card["question"])
                card["answer"]   = body.get("answer",   card["answer"])
                deck["updated_at"] = datetime.now().strftime("%d.%m.%Y %H:%M")
                save_data(data)
                return jsonify({"ok": True})
    return jsonify({"error": "Not found"}), 404

@app.route("/api/card/<int:card_id>", methods=["DELETE"])
def web_delete_card(card_id):
    data = load_data()
    for deck in data["decks"]:
        deck["cards"] = [c for c in deck["cards"] if c["id"] != card_id]
    save_data(data)
    return jsonify({"ok": True})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)