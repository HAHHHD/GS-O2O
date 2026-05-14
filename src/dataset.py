from __future__ import annotations

from typing import Any

import torch


class BranchDataset(torch.utils.data.Dataset):
    """
    Contains all D2 branch records:
        x
        action_id
        reward
        x_next
        greedy_action_id
        is_greedy
    """

    def __init__(self, data: dict[str, Any], greedy_only: bool = False, normalized_reward: bool = False) -> None:
        if isinstance(data, str):
            data = torch.load(data, map_location="cpu")
        self.x = data["x"].float()
        self.action_id = data["action_id"].long()
        self.reward_raw = data["reward"].float()
        self.reward_normalized = data.get("reward_normalized", self.reward_raw).float()
        self.reward = self.reward_normalized if normalized_reward else self.reward_raw
        self.x_next = data["x_next"].float()
        self.greedy_action_id = data["greedy_action_id"].long()
        self.is_greedy = data["is_greedy"].bool()
        self.t = data.get("t")
        self.circuit = data.get("circuit")
        if greedy_only:
            mask = self.is_greedy
            self.x = self.x[mask]
            self.action_id = self.action_id[mask]
            self.reward_raw = self.reward_raw[mask]
            self.reward_normalized = self.reward_normalized[mask]
            self.reward = self.reward[mask]
            self.x_next = self.x_next[mask]
            self.greedy_action_id = self.greedy_action_id[mask]
            self.is_greedy = self.is_greedy[mask]
            if self.t is not None:
                self.t = self.t[mask]
            if self.circuit is not None:
                indices = mask.nonzero(as_tuple=False).view(-1).tolist()
                self.circuit = [self.circuit[idx] for idx in indices]

    def __len__(self) -> int:
        return int(self.x.shape[0])

    def __getitem__(self, idx: int) -> dict[str, Any]:
        item = {
            "x": self.x[idx],
            "action_id": self.action_id[idx],
            "reward": self.reward[idx],
            "x_next": self.x_next[idx],
            "greedy_action_id": self.greedy_action_id[idx],
            "is_greedy": self.is_greedy[idx],
        }
        if self.t is not None:
            item["t"] = self.t[idx]
        if self.circuit is not None:
            item["circuit"] = self.circuit[idx]
        return item


class GreedyReturnDataset(torch.utils.data.Dataset):
    """
    Contains only executed greedy trajectory states:
        x_t
        G_t
    where G_t is Monte Carlo return under the greedy trajectory.
    """

    def __init__(self, data: dict[str, Any], normalized_return: bool = False) -> None:
        if isinstance(data, str):
            data = torch.load(data, map_location="cpu")
        self.x = data["x"].float()
        self.return_raw = data["return"].float()
        self.return_normalized = data.get("return_normalized", self.return_raw).float()
        self.return_ = self.return_normalized if normalized_return else self.return_raw
        self.t = data.get("t")
        self.circuit = data.get("circuit")

    def __len__(self) -> int:
        return int(self.x.shape[0])

    def __getitem__(self, idx: int) -> dict[str, Any]:
        item = {"x": self.x[idx], "return": self.return_[idx]}
        if self.t is not None:
            item["t"] = self.t[idx]
        if self.circuit is not None:
            item["circuit"] = self.circuit[idx]
        return item


class OnlineDataset(torch.utils.data.Dataset):
    """
    Contains online executed transitions:
        x_t
        action_id
        reward
        x_next
        return
    """

    def __init__(self, data: dict[str, Any] | list[dict[str, Any]]) -> None:
        if isinstance(data, list):
            if not data:
                self.x = torch.empty(0, 15)
                self.action_id = torch.empty(0, dtype=torch.long)
                self.reward = torch.empty(0)
                self.x_next = torch.empty(0, 15)
                self.return_ = torch.empty(0)
                self.circuit = []
                self.t = torch.empty(0, dtype=torch.long)
                return
            self.x = torch.as_tensor([row["x"] for row in data], dtype=torch.float32)
            self.action_id = torch.as_tensor([row["action_id"] for row in data], dtype=torch.long)
            self.reward = torch.as_tensor([row["reward"] for row in data], dtype=torch.float32)
            self.x_next = torch.as_tensor([row["x_next"] for row in data], dtype=torch.float32)
            self.return_ = torch.as_tensor([row["return"] for row in data], dtype=torch.float32)
            self.circuit = [row.get("circuit") for row in data]
            self.t = torch.as_tensor([row.get("t", 0) for row in data], dtype=torch.long)
        else:
            self.x = data["x"].float()
            self.action_id = data["action_id"].long()
            self.reward = data["reward"].float()
            self.x_next = data["x_next"].float()
            self.return_ = data["return"].float()
            self.circuit = data.get("circuit", [])
            self.t = data.get("t", torch.zeros(len(self.x), dtype=torch.long))

    def __len__(self) -> int:
        return int(self.x.shape[0])

    def __getitem__(self, idx: int) -> dict[str, Any]:
        item = {
            "x": self.x[idx],
            "action_id": self.action_id[idx],
            "reward": self.reward[idx],
            "x_next": self.x_next[idx],
            "return": self.return_[idx],
        }
        if len(self.circuit) > idx:
            item["circuit"] = self.circuit[idx]
        if len(self.t) > idx:
            item["t"] = self.t[idx]
        return item
