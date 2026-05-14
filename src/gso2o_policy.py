from __future__ import annotations

import random
from typing import Any

import numpy as np
import torch

from .utils import get_actions, num_actions


def _hybrid_cfg(source: dict[str, Any]) -> dict[str, Any]:
    return dict(source.get("gso2o_hybrid", {}))


def refine_cfg(source: dict[str, Any]) -> dict[str, Any]:
    return dict(source.get("gso2o_refine", {}))


def gso2o_mode_metadata(config: dict[str, Any]) -> dict[str, object]:
    hybrid = _hybrid_cfg(config)
    refine = refine_cfg(config)
    return {
        "immediate_reward_only": bool(config.get("ablation", {}).get("immediate_reward_only", False)),
        "hybrid_selector": bool(hybrid.get("enabled", False)),
        "topk_q": int(hybrid.get("selector_topk_q", 0)),
        "reward_regret_penalty": float(hybrid.get("reward_regret_penalty", 0.0)),
        "gso2o_refine": refine,
    }


def _progress_fraction(context: dict[str, Any] | None) -> float:
    if not context:
        return 0.0
    episode = int(context.get("episode", 0))
    total_episodes = max(int(context.get("total_episodes", 1)), 1)
    if total_episodes <= 1:
        return 1.0
    return min(max(episode / float(total_episodes - 1), 0.0), 1.0)


def _lerp(start: float, end: float, alpha: float) -> float:
    return start + (end - start) * alpha


def _action_family(name: str) -> str:
    if name.startswith("rewrite"):
        return "rewrite"
    if name.startswith("refactor"):
        return "refactor"
    if name.startswith("resub"):
        return "resub"
    if name.startswith("replace"):
        return "replace"
    return name


def _history_penalty(
    config: dict[str, Any],
    context: dict[str, Any] | None,
    device: torch.device,
) -> torch.Tensor:
    count = num_actions(config)
    penalties = torch.zeros(count, dtype=torch.float32, device=device)
    refine = refine_cfg(config)
    if not refine.get("enabled", False) or not context:
        return penalties

    history = [int(action_id) for action_id in context.get("action_history", [])]
    if not history:
        return penalties

    alpha = _progress_fraction(context)
    actions = get_actions(config)
    families = [_action_family(action["name"]) for action in actions]

    last_action = history[-1]
    streak = 1
    for action_id in reversed(history[:-1]):
        if action_id != last_action:
            break
        streak += 1

    last_family = families[last_action]
    family_streak = 1
    for action_id in reversed(history[:-1]):
        if families[action_id] != last_family:
            break
        family_streak += 1

    repeat_start = int(refine.get("repeat_streak_start", 4))
    repeat_penalty = _lerp(
        float(refine.get("repeat_streak_penalty_start", refine.get("repeat_streak_penalty", 0.0))),
        float(refine.get("repeat_streak_penalty_end", refine.get("repeat_streak_penalty", 0.0))),
        alpha,
    )
    if streak >= repeat_start and repeat_penalty > 0.0:
        penalties[last_action] += repeat_penalty * float(streak - repeat_start + 1)

    family_start = int(refine.get("family_streak_start", 6))
    family_penalty = _lerp(
        float(refine.get("family_streak_penalty_start", refine.get("family_streak_penalty", 0.0))),
        float(refine.get("family_streak_penalty_end", refine.get("family_streak_penalty", 0.0))),
        alpha,
    )
    if family_streak >= family_start and family_penalty > 0.0:
        for action_id, family in enumerate(families):
            if family == last_family:
                penalties[action_id] += family_penalty * float(family_streak - family_start + 1)

    recent_window = max(int(refine.get("recent_window", 0)), 0)
    recent_action_penalty = float(refine.get("recent_action_penalty", 0.0))
    if recent_window > 0 and recent_action_penalty > 0.0:
        recent = history[-recent_window:]
        counts = {action_id: recent.count(action_id) for action_id in set(recent)}
        for action_id, repeats in counts.items():
            penalties[action_id] += recent_action_penalty * float(repeats)

    recent_family_penalty = float(refine.get("recent_family_penalty", 0.0))
    if recent_window > 0 and recent_family_penalty > 0.0:
        recent = history[-recent_window:]
        family_counts: dict[str, int] = {}
        for action_id in recent:
            family = families[action_id]
            family_counts[family] = family_counts.get(family, 0) + 1
        for action_id, family in enumerate(families):
            penalties[action_id] += recent_family_penalty * float(family_counts.get(family, 0))

    return penalties


def _phase_bonus(
    config: dict[str, Any],
    context: dict[str, Any] | None,
    device: torch.device,
) -> torch.Tensor:
    count = num_actions(config)
    bonuses = torch.zeros(count, dtype=torch.float32, device=device)
    refine = refine_cfg(config)
    if not refine.get("enabled", False) or not context:
        return bonuses

    actions = get_actions(config)
    families = [_action_family(action["name"]) for action in actions]
    t = int(context.get("t", 0))
    horizon = max(int(context.get("horizon", 1)), 1)
    progress = t / float(horizon)
    early_end = float(refine.get("phase_early_end", 0.25))
    late_start = float(refine.get("phase_late_start", 0.75))
    if progress < early_end:
        phase_name = "early"
    elif progress >= late_start:
        phase_name = "late"
    else:
        phase_name = "mid"

    phase_biases = refine.get("phase_biases", {})
    phase_cfg = phase_biases.get(phase_name, {}) if isinstance(phase_biases, dict) else {}
    for action_id, action in enumerate(actions):
        bonuses[action_id] += float(phase_cfg.get(families[action_id], 0.0))
        bonuses[action_id] += float(phase_cfg.get(action["name"], 0.0))

    history = [int(action_id) for action_id in context.get("action_history", [])]
    if not history:
        return bonuses

    last_action = history[-1]
    last_family = families[last_action]
    family_streak = 1
    for action_id in reversed(history[:-1]):
        if families[action_id] != last_family:
            break
        family_streak += 1

    switch_rules = refine.get("switch_bonus_rules", [])
    if not isinstance(switch_rules, list):
        return bonuses
    for rule in switch_rules:
        if not isinstance(rule, dict):
            continue
        min_streak = int(rule.get("min_streak", 1))
        if family_streak < min_streak:
            continue
        source_families = set(rule.get("source_families", []))
        source_actions = set(rule.get("source_actions", []))
        if source_families or source_actions:
            if last_family not in source_families and actions[last_action]["name"] not in source_actions:
                continue
        bonus = float(rule.get("bonus", 0.0))
        target_families = set(rule.get("target_families", []))
        target_actions = set(rule.get("target_actions", []))
        for action_id, action in enumerate(actions):
            action_name = action["name"]
            action_family = families[action_id]
            if target_actions and action_name in target_actions:
                bonuses[action_id] += bonus
            elif target_families and action_family in target_families:
                bonuses[action_id] += bonus

    return bonuses


def select_action_gso2o(
    config: dict[str, Any],
    x_norm: np.ndarray,
    reward_model,
    q_ensemble,
    epsilon: float,
    beta: float,
    rho: float,
    ablation: dict[str, Any],
    context: dict[str, Any] | None = None,
) -> int:
    hybrid = _hybrid_cfg(config)
    refine = refine_cfg(config)
    action_count = num_actions(config)
    x_tensor = torch.as_tensor(x_norm, dtype=torch.float32).unsqueeze(0)
    action_ids = torch.arange(action_count, dtype=torch.long)
    with torch.no_grad():
        x_rep = x_tensor.repeat(action_count, 1)
        reward_preds = reward_model(x_rep, action_ids).cpu()
    if bool(ablation.get("immediate_reward_only", False)):
        return int(torch.argmax(reward_preds).item())

    greedy_action = int(torch.argmax(reward_preds).item())
    regrets = reward_preds[greedy_action] - reward_preds
    alpha = _progress_fraction(context)
    rho_scale = _lerp(
        float(refine.get("rho_scale_start", 1.0)),
        float(refine.get("rho_scale_end", 1.0)),
        alpha,
    )
    effective_rho = rho * rho_scale

    if bool(ablation.get("no_near_greedy_filter", False)):
        near_actions = torch.arange(action_count, dtype=torch.long)
    else:
        near_actions = torch.nonzero(regrets <= effective_rho, as_tuple=False).view(-1)
        if near_actions.numel() == 0:
            near_actions = torch.as_tensor([greedy_action], dtype=torch.long)

    with torch.no_grad():
        q_values = torch.stack([model.all_actions(x_tensor).squeeze(0) for model in q_ensemble], dim=0)
        mean_q = q_values.mean(dim=0)
        std_q = q_values.std(dim=0, unbiased=False)

    hybrid_enabled = bool(hybrid.get("enabled", False))
    topk_q = max(0, min(action_count, int(hybrid.get("selector_topk_q", 0))))
    if refine.get("enabled", False):
        topk_q = max(topk_q, min(action_count, int(refine.get("topk_q_min", topk_q))))

    regret_penalty = torch.zeros_like(regrets)
    if hybrid_enabled:
        regret_eta = float(hybrid.get("reward_regret_penalty", 0.0))
        if refine.get("enabled", False):
            regret_eta *= _lerp(
                float(refine.get("reward_regret_penalty_scale_start", 1.0)),
                float(refine.get("reward_regret_penalty_scale_end", 1.0)),
                alpha,
            )
        if regret_eta > 0.0:
            regret_penalty = regret_eta * torch.clamp(regrets - effective_rho, min=0.0)

    history_penalty = _history_penalty(config, context, device=mean_q.device)
    phase_bonus = _phase_bonus(config, context, device=mean_q.device)
    pessimism = 0.0
    if refine.get("enabled", False):
        pessimism = _lerp(
            float(refine.get("exploit_pessimism_start", 0.0)),
            float(refine.get("exploit_pessimism_end", 0.0)),
            alpha,
        )
    exploit_base = mean_q - pessimism * std_q

    candidate_base = exploit_base - regret_penalty - history_penalty + phase_bonus
    if hybrid_enabled and topk_q > 0:
        topk_idx = torch.topk(candidate_base, k=topk_q).indices
        near_actions = torch.unique(torch.cat([near_actions, topk_idx], dim=0))

    if random.random() > epsilon:
        if hybrid_enabled:
            exploit_scores = candidate_base[near_actions]
            return int(near_actions[torch.argmax(exploit_scores)].item())
        return int(torch.argmax(mean_q).item())

    if bool(ablation.get("no_uncertainty", False)):
        choice = random.choice(near_actions.tolist())
        return int(choice)

    explore_history_scale = float(refine.get("explore_history_penalty_scale", 1.0)) if refine.get("enabled", False) else 1.0
    near_scores = (
        mean_q[near_actions]
        + beta * std_q[near_actions]
        - regret_penalty[near_actions]
        - explore_history_scale * history_penalty[near_actions]
        + phase_bonus[near_actions]
    )
    return int(near_actions[torch.argmax(near_scores)].item())
