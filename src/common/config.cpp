#include "common/config.h"
#include "common/logger.h"
#include "common/constants.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>
#include <vector>
#include <algorithm>
#include <cctype>

namespace cuas {

namespace {

// Minimal JSON parser sufficient for flat and nested config structures.
// Production systems would use nlohmann/json or RapidJSON.

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray  = std::vector<JsonValue>;

class JsonValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    JsonValue() : type_(Null) {}
    explicit JsonValue(bool b) : type_(Bool), bVal_(b) {}
    explicit JsonValue(double d) : type_(Number), dVal_(d) {}
    explicit JsonValue(const std::string& s) : type_(String), sVal_(s) {}
    explicit JsonValue(const JsonArray& a) : type_(Array), aVal_(a) {}
    explicit JsonValue(const JsonObject& o) : type_(Object), oVal_(o) {}

    Type type() const { return type_; }
    bool asBool() const { return bVal_; }
    double asNumber() const { return dVal_; }
    int asInt() const { return static_cast<int>(dVal_); }
    const std::string& asString() const { return sVal_; }
    const JsonArray& asArray() const { return aVal_; }
    const JsonObject& asObject() const { return oVal_; }

    bool has(const std::string& key) const {
        return type_ == Object && oVal_.count(key) > 0;
    }

    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null;
        if (type_ != Object) return null;
        auto it = oVal_.find(key);
        return it != oVal_.end() ? it->second : null;
    }

    const JsonValue& operator[](size_t idx) const {
        static JsonValue null;
        if (type_ != Array || idx >= aVal_.size()) return null;
        return aVal_[idx];
    }

private:
    Type type_;
    bool bVal_ = false;
    double dVal_ = 0.0;
    std::string sVal_;
    JsonArray aVal_;
    JsonObject oVal_;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text), pos_(0) {}

    JsonValue parse() {
        skipWhitespace();
        return parseValue();
    }

private:
    void skipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char advance() { return pos_ < text_.size() ? text_[pos_++] : '\0'; }

    JsonValue parseValue() {
        skipWhitespace();
        char c = peek();
        if (c == '"') return JsonValue(parseString());
        if (c == '{') return JsonValue(parseObject());
        if (c == '[') return JsonValue(parseArray());
        if (c == 't' || c == 'f') return JsonValue(parseBool());
        if (c == 'n') { parseNull(); return JsonValue(); }
        return JsonValue(parseNumber());
    }

    std::string parseString() {
        advance(); // opening quote
        std::string s;
        while (pos_ < text_.size()) {
            char c = advance();
            if (c == '"') break;
            if (c == '\\') {
                char next = advance();
                switch (next) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    default: s += next; break;
                }
            } else {
                s += c;
            }
        }
        return s;
    }

    double parseNumber() {
        size_t start = pos_;
        if (peek() == '-') advance();
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
               text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
               text_[pos_] == '+' || text_[pos_] == '-')) {
            if ((text_[pos_] == '+' || text_[pos_] == '-') && pos_ > start &&
                text_[pos_ - 1] != 'e' && text_[pos_ - 1] != 'E') break;
            advance();
        }
        return std::stod(text_.substr(start, pos_ - start));
    }

    bool parseBool() {
        if (text_.substr(pos_, 4) == "true") { pos_ += 4; return true; }
        if (text_.substr(pos_, 5) == "false") { pos_ += 5; return false; }
        throw std::runtime_error("Invalid bool");
    }

    void parseNull() { pos_ += 4; }

    JsonObject parseObject() {
        advance(); // '{'
        JsonObject obj;
        skipWhitespace();
        if (peek() == '}') { advance(); return obj; }
        while (true) {
            skipWhitespace();
            std::string key = parseString();
            skipWhitespace();
            advance(); // ':'
            obj[key] = parseValue();
            skipWhitespace();
            if (peek() == ',') { advance(); continue; }
            if (peek() == '}') { advance(); break; }
        }
        return obj;
    }

    JsonArray parseArray() {
        advance(); // '['
        JsonArray arr;
        skipWhitespace();
        if (peek() == ']') { advance(); return arr; }
        while (true) {
            arr.push_back(parseValue());
            skipWhitespace();
            if (peek() == ',') { advance(); continue; }
            if (peek() == ']') { advance(); break; }
        }
        return arr;
    }

    std::string text_;
    size_t pos_;
};

} // anonymous namespace

TrackerConfig loadConfig(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    JsonParser parser(content);
    JsonValue root = parser.parse();

    TrackerConfig cfg;

    // System
    if (root.has("system")) {
        auto& s = root["system"];
        cfg.system.cyclePeriodMs        = s["cyclePeriodMs"].asInt();
        cfg.system.maxDetectionsPerDwell = s["maxDetectionsPerDwell"].asInt();
        cfg.system.maxTracks            = s["maxTracks"].asInt();
        cfg.system.logDirectory         = s["logDirectory"].asString();
        cfg.system.logEnabled           = s["logEnabled"].asBool();
        cfg.system.logLevel             = s["logLevel"].asInt();
    }

    // Network
    if (root.has("network")) {
        auto& n = root["network"];
        cfg.network.receiverIp       = n["receiverIp"].asString();
        cfg.network.receiverPort     = n["receiverPort"].asInt();
        cfg.network.senderIp         = n["senderIp"].asString();
        cfg.network.senderPort       = n["senderPort"].asInt();
        cfg.network.receiveBufferSize = n["receiveBufferSize"].asInt();
        cfg.network.sendBufferSize   = n["sendBufferSize"].asInt();
    }

    // Preprocessing
    if (root.has("preprocessing")) {
        auto& p = root["preprocessing"];
        cfg.preprocessing.minRange     = p["minRange"].asNumber();
        cfg.preprocessing.maxRange     = p["maxRange"].asNumber();
        cfg.preprocessing.minAzimuth   = p["minAzimuth"].asNumber();
        cfg.preprocessing.maxAzimuth   = p["maxAzimuth"].asNumber();
        cfg.preprocessing.minElevation = p["minElevation"].asNumber();
        cfg.preprocessing.maxElevation = p["maxElevation"].asNumber();
        cfg.preprocessing.minSNR       = p["minSNR"].asNumber();
        cfg.preprocessing.maxSNR       = p["maxSNR"].asNumber();
        cfg.preprocessing.minRCS       = p["minRCS"].asNumber();
        cfg.preprocessing.maxRCS       = p["maxRCS"].asNumber();
        cfg.preprocessing.minStrength  = p["minStrength"].asNumber();
        cfg.preprocessing.maxStrength  = p["maxStrength"].asNumber();
    }

    // Clustering
    if (root.has("clustering")) {
        auto& c = root["clustering"];
        std::string method = c["method"].asString();
        if (method == "dbscan") cfg.clustering.method = ClusterMethod::DBSCAN;
        else if (method == "range_based") cfg.clustering.method = ClusterMethod::RangeBased;
        else if (method == "range_strength") cfg.clustering.method = ClusterMethod::RangeStrengthBased;

        if (c.has("dbscan")) {
            auto& d = c["dbscan"];
            cfg.clustering.dbscan.epsilonRange     = d["epsilonRange"].asNumber();
            cfg.clustering.dbscan.epsilonAzimuth   = d["epsilonAzimuth"].asNumber();
            cfg.clustering.dbscan.epsilonElevation = d["epsilonElevation"].asNumber();
            cfg.clustering.dbscan.minPoints        = d["minPoints"].asInt();
        }
        if (c.has("rangeBased")) {
            auto& r = c["rangeBased"];
            cfg.clustering.rangeBased.rangeGateSize     = r["rangeGateSize"].asNumber();
            cfg.clustering.rangeBased.azimuthGateSize   = r["azimuthGateSize"].asNumber();
            cfg.clustering.rangeBased.elevationGateSize = r["elevationGateSize"].asNumber();
        }
        if (c.has("rangeStrength")) {
            auto& r = c["rangeStrength"];
            cfg.clustering.rangeStrength.rangeGateSize     = r["rangeGateSize"].asNumber();
            cfg.clustering.rangeStrength.azimuthGateSize   = r["azimuthGateSize"].asNumber();
            cfg.clustering.rangeStrength.elevationGateSize = r["elevationGateSize"].asNumber();
            cfg.clustering.rangeStrength.strengthGateSize  = r["strengthGateSize"].asNumber();
        }
    }

    // Prediction
    if (root.has("prediction")) {
        auto& p = root["prediction"];
        if (p.has("imm")) {
            auto& imm = p["imm"];
            cfg.prediction.imm.numModels = imm["numModels"].asInt();
            auto& ip = imm["initialModeProbabilities"].asArray();
            for (size_t i = 0; i < ip.size() && i < IMM_NUM_MODELS; ++i)
                cfg.prediction.imm.initialModeProbabilities[i] = ip[i].asNumber();
            auto& tm = imm["transitionMatrix"].asArray();
            for (size_t i = 0; i < tm.size() && i < IMM_NUM_MODELS; ++i) {
                auto& row = tm[i].asArray();
                for (size_t j = 0; j < row.size() && j < IMM_NUM_MODELS; ++j)
                    cfg.prediction.imm.transitionMatrix[i][j] = row[j].asNumber();
            }
        }
        if (p.has("cv")) {
            cfg.prediction.cv.processNoiseStd = p["cv"]["processNoiseStd"].asNumber();
        }
        if (p.has("ca1")) {
            cfg.prediction.ca1.processNoiseStd = p["ca1"]["processNoiseStd"].asNumber();
            cfg.prediction.ca1.accelDecayRate  = p["ca1"]["accelDecayRate"].asNumber();
        }
        if (p.has("ca2")) {
            cfg.prediction.ca2.processNoiseStd = p["ca2"]["processNoiseStd"].asNumber();
            cfg.prediction.ca2.accelDecayRate  = p["ca2"]["accelDecayRate"].asNumber();
        }
        if (p.has("ctr1")) {
            cfg.prediction.ctr1.processNoiseStd  = p["ctr1"]["processNoiseStd"].asNumber();
            cfg.prediction.ctr1.turnRateNoiseStd = p["ctr1"]["turnRateNoiseStd"].asNumber();
        }
        if (p.has("ctr2")) {
            cfg.prediction.ctr2.processNoiseStd  = p["ctr2"]["processNoiseStd"].asNumber();
            cfg.prediction.ctr2.turnRateNoiseStd = p["ctr2"]["turnRateNoiseStd"].asNumber();
        }
    }

    // Association
    if (root.has("association")) {
        auto& a = root["association"];
        std::string method = a["method"].asString();
        if (method == "mahalanobis") cfg.association.method = AssociationMethod::Mahalanobis;
        else if (method == "gnn") cfg.association.method = AssociationMethod::GNN;
        else if (method == "jpda") cfg.association.method = AssociationMethod::JPDA;

        cfg.association.gatingThreshold = a["gatingThreshold"].asNumber();

        if (a.has("mahalanobis")) {
            cfg.association.mahalanobis.distanceThreshold =
                a["mahalanobis"]["distanceThreshold"].asNumber();
        }
        if (a.has("gnn")) {
            cfg.association.gnn.costThreshold = a["gnn"]["costThreshold"].asNumber();
        }
        if (a.has("jpda")) {
            auto& j = a["jpda"];
            cfg.association.jpda.gateSize             = j["gateSize"].asNumber();
            cfg.association.jpda.clutterDensity       = j["clutterDensity"].asNumber();
            cfg.association.jpda.detectionProbability  = j["detectionProbability"].asNumber();
        }
    }

    // Track management
    if (root.has("trackManagement")) {
        auto& t = root["trackManagement"];
        if (t.has("initiation")) {
            auto& i = t["initiation"];
            cfg.trackManagement.initiation.method = i["method"].asString();
            cfg.trackManagement.initiation.m      = i["m"].asInt();
            cfg.trackManagement.initiation.n      = i["n"].asInt();
            cfg.trackManagement.initiation.maxInitiationRange = i["maxInitiationRange"].asNumber();
            cfg.trackManagement.initiation.velocityGate = i["velocityGate"].asNumber();
        }
        if (t.has("maintenance")) {
            auto& m = t["maintenance"];
            cfg.trackManagement.maintenance.confirmHits       = m["confirmHits"].asInt();
            cfg.trackManagement.maintenance.coastingLimit      = m["coastingLimit"].asInt();
            cfg.trackManagement.maintenance.deleteAfterMisses  = m["deleteAfterMisses"].asInt();
            cfg.trackManagement.maintenance.qualityDecayRate   = m["qualityDecayRate"].asNumber();
            cfg.trackManagement.maintenance.qualityBoost       = m["qualityBoost"].asNumber();
            cfg.trackManagement.maintenance.minQualityThreshold = m["minQualityThreshold"].asNumber();
        }
        if (t.has("deletion")) {
            auto& d = t["deletion"];
            cfg.trackManagement.deletion.maxCoastingDwells = d["maxCoastingDwells"].asInt();
            cfg.trackManagement.deletion.minQuality        = d["minQuality"].asNumber();
            cfg.trackManagement.deletion.maxRange          = d["maxRange"].asNumber();
        }
        if (t.has("initialCovariance")) {
            auto& ic = t["initialCovariance"];
            cfg.trackManagement.initialCovariance.positionStd     = ic["positionStd"].asNumber();
            cfg.trackManagement.initialCovariance.velocityStd     = ic["velocityStd"].asNumber();
            cfg.trackManagement.initialCovariance.accelerationStd = ic["accelerationStd"].asNumber();
        }
    }

    // Display
    if (root.has("display")) {
        auto& d = root["display"];
        cfg.display.updateRateMs      = d["updateRateMs"].asInt();
        cfg.display.sendDeletedTracks = d["sendDeletedTracks"].asBool();
    }

    LOG_INFO("Config", "Configuration loaded from %s", filepath.c_str());
    return cfg;
}

} // namespace cuas
