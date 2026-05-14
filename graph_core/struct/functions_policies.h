#pragma once

#include <queue>
#include <stack>

/**
 * ---- Traversal Policies ----
 * A policy defines the frontier behavior for graph traversal.
 * Swapping the policy changes the traversal order without touching the algorithm.
 */

struct BFSPolicy
{
    using Frontier = std::queue<int>;
    static void push(Frontier &f, int i) { f.push(i); }
    static int pop(Frontier &f)
    {
        int v = f.front();
        f.pop();
        return v;
    }
    static bool empty(Frontier &f) { return f.empty(); }
};

struct DFSPolicy
{
    using Frontier = std::stack<int>;
    static void push(Frontier &f, int i) { f.push(i); }
    static int pop(Frontier &f)
    {
        int v = f.top();
        f.pop();
        return v;
    }
    static bool empty(Frontier &f) { return f.empty(); }
};