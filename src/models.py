from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F


def action_one_hot(action_id: torch.Tensor, num_actions: int) -> torch.Tensor:
    return F.one_hot(action_id.long(), num_classes=num_actions).float()


def _activation(name: str) -> nn.Module:
    name = name.lower()
    if name == "relu":
        return nn.ReLU()
    if name == "gelu":
        return nn.GELU()
    return nn.LeakyReLU(negative_slope=0.1)


def _build_mlp(input_dim: int, output_dim: int, hidden_dim: int, num_layers: int, activation: str) -> nn.Sequential:
    layers: list[nn.Module] = []
    prev_dim = input_dim
    hidden_layers = max(num_layers, 1)
    for _ in range(hidden_layers):
        layers.append(nn.Linear(prev_dim, hidden_dim))
        layers.append(_activation(activation))
        prev_dim = hidden_dim
    layers.append(nn.Linear(prev_dim, output_dim))
    return nn.Sequential(*layers)


class RewardModel(nn.Module):
    def __init__(self, feature_dim: int, num_actions: int, hidden_dim: int = 256, num_layers: int = 2, activation: str = "leaky_relu"):
        super().__init__()
        self.num_actions = num_actions
        self.net = _build_mlp(feature_dim + num_actions, 1, hidden_dim, num_layers, activation)

    def forward(self, x: torch.Tensor, action_id: torch.Tensor) -> torch.Tensor:
        """
        Input:
            x: [B, feature_dim]
            action_id: [B]
        Output:
            reward_pred: [B]
        """
        inputs = torch.cat([x, action_one_hot(action_id, self.num_actions)], dim=-1)
        return self.net(inputs).squeeze(-1)


class ValueModel(nn.Module):
    def __init__(self, feature_dim: int, hidden_dim: int = 256, num_layers: int = 2, activation: str = "leaky_relu"):
        super().__init__()
        self.net = _build_mlp(feature_dim, 1, hidden_dim, num_layers, activation)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """
        Input:
            x: [B, feature_dim]
        Output:
            value: [B]
        """
        return self.net(x).squeeze(-1)


class QModel(nn.Module):
    def __init__(self, feature_dim: int, num_actions: int, hidden_dim: int = 256, num_layers: int = 2, activation: str = "leaky_relu"):
        super().__init__()
        self.num_actions = num_actions
        self.net = _build_mlp(feature_dim + num_actions, 1, hidden_dim, num_layers, activation)

    def forward(self, x: torch.Tensor, action_id: torch.Tensor) -> torch.Tensor:
        """
        Input:
            x: [B, feature_dim]
            action_id: [B]
        Output:
            q_value: [B]
        """
        inputs = torch.cat([x, action_one_hot(action_id, self.num_actions)], dim=-1)
        return self.net(inputs).squeeze(-1)

    def all_actions(self, x: torch.Tensor) -> torch.Tensor:
        """
        Input:
            x: [B, feature_dim]
        Output:
            q_values: [B, num_actions]
        """
        batch_size = x.shape[0]
        all_actions = torch.arange(self.num_actions, device=x.device).unsqueeze(0).repeat(batch_size, 1)
        x_rep = x.unsqueeze(1).repeat(1, self.num_actions, 1)
        inputs = torch.cat([x_rep, action_one_hot(all_actions, self.num_actions)], dim=-1)
        return self.net(inputs.view(batch_size * self.num_actions, -1)).view(batch_size, self.num_actions)


class PolicyModel(nn.Module):
    def __init__(self, feature_dim: int, num_actions: int, hidden_dim: int = 256, num_layers: int = 2, activation: str = "leaky_relu"):
        super().__init__()
        self.net = _build_mlp(feature_dim, num_actions, hidden_dim, num_layers, activation)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)

    def log_probs(self, x: torch.Tensor) -> torch.Tensor:
        return F.log_softmax(self.forward(x), dim=-1)

    def probs(self, x: torch.Tensor) -> torch.Tensor:
        return F.softmax(self.forward(x), dim=-1)
