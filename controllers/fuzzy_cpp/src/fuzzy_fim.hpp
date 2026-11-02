#pragma once

#include <fl/Headers.h>

#include <algorithm>
#include <string>
#include <vector>

/*
 * Fuzzy Inference Machine (FIM): two-input, one-output Mamdani system.
 *
 *  - Inputs  : error, delta_error  (normalized universe [-1, 1])
 *  - Output  : output              (normalized universe [-1, 1])
 *  - 7 linguistic terms per variable: LN, MN, SN, ZZ, SP, MP, LP
 *    (trapezoids at the extremes, triangles in between)
 *  - 49 rules (full 7x7 PI-like table)
 *  - AND = Minimum, implication = Minimum, aggregation = Maximum,
 *    activation = General (fuzzylite default), defuzzifier = Centroid with
 *    100 samples, default output 0 when no rule fires.
 *
 * The engine is NOT thread-safe: callers must serialize compute().
 */
class FuzzyFIM {
public:
    FuzzyFIM() {
        using namespace fl;

        engine_.setName("FIM-Controller");

        error_ = new InputVariable;
        error_->setName("error");
        error_->setRange(-1.000, 1.000);
        addTerms(error_);
        engine_.addInputVariable(error_);

        delta_ = new InputVariable;
        delta_->setName("delta_error");
        delta_->setRange(-1.000, 1.000);
        addTerms(delta_);
        engine_.addInputVariable(delta_);

        output_ = new OutputVariable;
        output_->setName("output");
        output_->setRange(-1.000, 1.000);
        output_->setAggregation(new Maximum());
        output_->setDefuzzifier(new Centroid(100));
        output_->setDefaultValue(0.000);
        addTerms(output_);
        engine_.addOutputVariable(output_);

        RuleBlock* rules = new RuleBlock;
        rules->setConjunction(new Minimum());
        rules->setImplication(new Minimum());

        // Rows: error term; columns: delta_error term.
        static const std::vector<std::string> labels = {"LN", "MN", "SN", "ZZ", "SP", "MP", "LP"};
        static const char* matrix[7][7] = {
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
                rules->addRule(Rule::parse(r, &engine_));
            }
        }
        engine_.addRuleBlock(rules);
    }

    // Inputs are clamped to the universe of discourse before inference.
    double compute(double e, double de) {
        error_->setValue(std::clamp(e, -1.0, 1.0));
        delta_->setValue(std::clamp(de, -1.0, 1.0));
        engine_.process();
        return output_->getValue();
    }

private:
    static void addTerms(fl::Variable* var) {
        using namespace fl;
        var->addTerm(new Trapezoid("LN", -1.000, -1.000, -0.700, -0.400));
        var->addTerm(new Triangle("MN", -0.700, -0.400, -0.100));
        var->addTerm(new Triangle("SN", -0.400, -0.100, 0.000));
        var->addTerm(new Triangle("ZZ", -0.100, 0.000, 0.100));
        var->addTerm(new Triangle("SP", 0.000, 0.100, 0.400));
        var->addTerm(new Triangle("MP", 0.100, 0.400, 0.700));
        var->addTerm(new Trapezoid("LP", 0.400, 0.700, 1.000, 1.000));
    }

    fl::Engine engine_;  // owns variables and rule blocks
    fl::InputVariable* error_;
    fl::InputVariable* delta_;
    fl::OutputVariable* output_;
};
