#include <fl/Headers.h>
#include "httplib.h"
#include "json.hpp"
#include <iostream>
#include <algorithm>
#include <vector>

using namespace fl;
using json = nlohmann::json;

class FuzzyFIM {
public:
    Engine* engine;
    InputVariable* error;
    InputVariable* delta;
    OutputVariable* output;
    RuleBlock* ruleBlock;

    FuzzyFIM() {
        engine = new Engine;
        engine->setName("FIM-Controller");

        // 1. Input: Error
        error = new InputVariable;
        error->setName("error");
        error->setRange(-1.000, 1.000);
        error->addTerm(new Trapezoid("LN", -1.000, -1.000, -0.700, -0.400));
        error->addTerm(new Triangle("MN", -0.700, -0.400, -0.100));
        error->addTerm(new Triangle("SN", -0.400, -0.100, 0.000));
        error->addTerm(new Triangle("ZZ", -0.100, 0.000, 0.100));
        error->addTerm(new Triangle("SP", 0.000, 0.100, 0.400));
        error->addTerm(new Triangle("MP", 0.100, 0.400, 0.700));
        error->addTerm(new Trapezoid("LP", 0.400, 0.700, 1.000, 1.000));
        engine->addInputVariable(error);

        // 2. Input: Delta Error
        delta = new InputVariable;
        delta->setName("delta_error");
        delta->setRange(-1.000, 1.000);
        delta->addTerm(new Trapezoid("LN", -1.000, -1.000, -0.700, -0.400));
        delta->addTerm(new Triangle("MN", -0.700, -0.400, -0.100));
        delta->addTerm(new Triangle("SN", -0.400, -0.100, 0.000));
        delta->addTerm(new Triangle("ZZ", -0.100, 0.000, 0.100));
        delta->addTerm(new Triangle("SP", 0.000, 0.100, 0.400));
        delta->addTerm(new Triangle("MP", 0.100, 0.400, 0.700));
        delta->addTerm(new Trapezoid("LP", 0.400, 0.700, 1.000, 1.000));
        engine->addInputVariable(delta);

        // 3. Output: Increment
        output = new OutputVariable;
        output->setName("output");
        output->setRange(-1.000, 1.000);
        output->setAggregation(new Maximum());
        output->setDefuzzifier(new Centroid(100));
        output->setDefaultValue(0.000);
        output->addTerm(new Trapezoid("LN", -1.000, -1.000, -0.700, -0.400));
        output->addTerm(new Triangle("MN", -0.700, -0.400, -0.100));
        output->addTerm(new Triangle("SN", -0.400, -0.100, 0.000));
        output->addTerm(new Triangle("ZZ", -0.100, 0.000, 0.100));
        output->addTerm(new Triangle("SP", 0.000, 0.100, 0.400));
        output->addTerm(new Triangle("MP", 0.100, 0.400, 0.700));
        output->addTerm(new Trapezoid("LP", 0.400, 0.700, 1.000, 1.000));
        engine->addOutputVariable(output);

        // 4. Rule Block (Mamdani)
        ruleBlock = new RuleBlock;
        ruleBlock->setConjunction(new Minimum());
        ruleBlock->setImplication(new Minimum());

        std::vector<std::string> labels = {"LN", "MN", "SN", "ZZ", "SP", "MP", "LP"};
        std::string matrix[7][7] = {
            {"LN", "LN", "LN", "LN", "MN", "SN", "ZZ"}, // LN
            {"LN", "LN", "LN", "MN", "SN", "ZZ", "SP"}, // MN
            {"LN", "LN", "MN", "SN", "ZZ", "SP", "MP"}, // SN
            {"LN", "MN", "SN", "ZZ", "SP", "MP", "LP"}, // ZZ
            {"MN", "SN", "ZZ", "SP", "MP", "LP", "LP"}, // SP
            {"SN", "ZZ", "SP", "MP", "LP", "LP", "LP"}, // MP
            {"ZZ", "SP", "MP", "LP", "LP", "LP", "LP"}  // LP
        };

        for (int i = 0; i < 7; ++i) {
            for (int j = 0; j < 7; ++j) {
                std::string r = "if error is " + labels[i] + 
                                " and delta_error is " + labels[j] + 
                                " then output is " + matrix[i][j];
                ruleBlock->addRule(Rule::parse(r, engine));
            }
        }
        engine->addRuleBlock(ruleBlock);
    }

    double compute(double e, double de) {
        error->setValue(std::max(-1.0, std::min(1.0, e)));
        delta->setValue(std::max(-1.0, std::min(1.0, de)));
        engine->process();
        return output->getValue();
    }
};

int main() {
    FuzzyFIM controller;
    httplib::Server svr;

    svr.Post("/control", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto data = json::parse(req.body);
            double e = data.value("error", 0.0);
            double de = data.value("delta_error", 0.0);

            double u = controller.compute(e, de);

            json response;
            response["u"] = u;
            res.set_content(response.dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\": \"Invalid JSON or Processing Error\"}", "application/json");
        }
    });

    std::cout << "Fuzzy Inference Machine (FIM) running on port 5000..." << std::endl;
    svr.listen("0.0.0.0", 5000);
    return 0;
}