#include "office_breach_mode.h"

void OfficeBreachMode::init(const Scenario& scn,
                            const std::vector<ScenarioEntity>& /*entities*/) {
    total_rounds_ = scn.rounds;
    objective_done_ = false;
}

GameModeResult OfficeBreachMode::on_round_end(
    int round, const std::vector<ScenarioEntity>& entities) {
    auto alive_ops = [&](int team) {
        int n = 0;
        for (const auto& e : entities)
            if (e.team == team && e.kind == EntityKind::Operator && e.hp > 0)
                ++n;
        return n;
    };
    int attackers = alive_ops(attacker_team_);
    int defenders = alive_ops(defender_team_);

    if (objective_done_) {
        GameModeResult r;
        r.finished = true;
        r.winning_team = attacker_team_;
        r.reason = "attacker interacted with objective";
        return r;
    }
    if (attackers == 0) {
        GameModeResult r;
        r.finished = true;
        r.winning_team = defender_team_;
        r.reason = "all attackers eliminated";
        return r;
    }
    if (defenders == 0) {
        GameModeResult r;
        r.finished = true;
        r.winning_team = attacker_team_;
        r.reason = "all defenders eliminated";
        return r;
    }
    if (round >= total_rounds_ - 1) {
        GameModeResult r;
        r.finished = true;
        r.winning_team = defender_team_;
        r.reason = "round limit reached";
        return r;
    }
    return GameModeResult{};
}
