#include <bits/stdc++.h>
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>

using namespace std;
using namespace __gnu_pbds;

enum class JudgeStatus : int {
    Accepted = 0,
    WrongAnswer = 1,
    RuntimeError = 2,
    TimeLimitExceeded = 3,
};

struct Submission {
    int problem = 0;
    JudgeStatus status = JudgeStatus::WrongAnswer;
    int time = 0;
};

struct ProblemState {
    int wrong = 0;                 // visible wrong submissions before first accepted
    bool solved = false;
    int solveTime = 0;

    // current freeze-session states
    bool freezeEligible = false;   // unsolved when freeze starts
    bool frozen = false;           // has post-freeze submissions while freezeEligible
    int frozenWrongBase = 0;       // wrong count at freeze start
    int frozenSubmitCount = 0;     // submissions after freeze (for display)
    vector<Submission> hiddenSubs; // hidden submissions collected while frozen
};

struct Team {
    string name;
    vector<ProblemState> problems;

    int solvedCount = 0;
    int penalty = 0;
    vector<int> solveTimesDesc; // descending

    vector<Submission> submissions;
    // [problem: 0..25, ALL=26][status: 0..3, ALL=4]
    array<array<int, 5>, 27> lastIdx{};
    set<int> frozenProblems;

    Team() {
        for (auto &row : lastIdx) row.fill(-1);
    }
};

static vector<Team> g_teams;

struct RankCmp {
    bool operator()(int a, int b) const {
        if (a == b) return false;
        const Team &A = g_teams[a];
        const Team &B = g_teams[b];
        if (A.solvedCount != B.solvedCount) return A.solvedCount > B.solvedCount;
        if (A.penalty != B.penalty) return A.penalty < B.penalty;
        for (int i = 0; i < A.solvedCount; ++i) {
            if (A.solveTimesDesc[i] != B.solveTimesDesc[i]) {
                return A.solveTimesDesc[i] < B.solveTimesDesc[i];
            }
        }
        return A.name < B.name;
    }
};

using OrderTree = tree<int, null_type, RankCmp, rb_tree_tag, tree_order_statistics_node_update>;

struct Contest {
    unordered_map<string, int> idByName;
    bool started = false;
    bool frozen = false;
    int duration = 0;
    int problemCount = 0;

    OrderTree rankingTree;
    OrderTree frozenTeamTree; // teams with non-empty frozenProblems

    vector<int> lastFlushedRank; // by team id, 1-indexed rank

    static JudgeStatus parseStatus(const string &s) {
        if (s == "Accepted") return JudgeStatus::Accepted;
        if (s == "Wrong_Answer") return JudgeStatus::WrongAnswer;
        if (s == "Runtime_Error") return JudgeStatus::RuntimeError;
        return JudgeStatus::TimeLimitExceeded;
    }

    static string statusToString(JudgeStatus st) {
        switch (st) {
            case JudgeStatus::Accepted: return "Accepted";
            case JudgeStatus::WrongAnswer: return "Wrong_Answer";
            case JudgeStatus::RuntimeError: return "Runtime_Error";
            case JudgeStatus::TimeLimitExceeded: return "Time_Limit_Exceed";
        }
        return "";
    }

    static int statusIndex(JudgeStatus st) {
        return static_cast<int>(st);
    }

    void addTeam(const string &name) {
        if (started) {
            cout << "[Error]Add failed: competition has started.\n";
            return;
        }
        if (idByName.count(name)) {
            cout << "[Error]Add failed: duplicated team name.\n";
            return;
        }
        int id = (int)g_teams.size();
        Team t;
        t.name = name;
        g_teams.push_back(std::move(t));
        idByName[name] = id;
        cout << "[Info]Add successfully.\n";
    }

    void start(int d, int m) {
        if (started) {
            cout << "[Error]Start failed: competition has started.\n";
            return;
        }
        started = true;
        duration = d;
        problemCount = m;

        for (auto &team : g_teams) {
            team.problems.assign(problemCount, ProblemState{});
            team.solvedCount = 0;
            team.penalty = 0;
            team.solveTimesDesc.clear();
            team.submissions.clear();
            for (auto &row : team.lastIdx) row.fill(-1);
            team.frozenProblems.clear();
        }

        rankingTree.clear();
        frozenTeamTree.clear();
        lastFlushedRank.assign(g_teams.size(), 0);

        for (int id = 0; id < (int)g_teams.size(); ++id) {
            rankingTree.insert(id);
        }
        refreshLastFlushedRank(); // before first explicit FLUSH, rank is lexicographic

        cout << "[Info]Competition starts.\n";
    }

    static void applyVisibleSubmission(Team &team, int p, JudgeStatus st, int tm) {
        ProblemState &ps = team.problems[p];
        if (ps.solved) return;
        if (st == JudgeStatus::Accepted) {
            ps.solved = true;
            ps.solveTime = tm;
            team.solvedCount += 1;
            team.penalty += ps.wrong * 20 + tm;
            auto it = lower_bound(team.solveTimesDesc.begin(), team.solveTimesDesc.end(), tm, greater<int>());
            team.solveTimesDesc.insert(it, tm);
        } else {
            ps.wrong += 1;
        }
    }

    void submit(int p, const string &teamName, JudgeStatus st, int tm) {
        int id = idByName[teamName];
        Team &team = g_teams[id];

        Submission sub{p, st, tm};
        team.submissions.push_back(sub);
        int idx = (int)team.submissions.size() - 1;
        int sidx = statusIndex(st);
        team.lastIdx[p][sidx] = idx;
        team.lastIdx[p][4] = idx;
        team.lastIdx[26][sidx] = idx;
        team.lastIdx[26][4] = idx;

        bool needRankUpdate = false;
        if (frozen) {
            ProblemState &ps = team.problems[p];
            if (ps.freezeEligible) {
                if (!ps.frozen) {
                    ps.frozen = true;
                    team.frozenProblems.insert(p);
                    frozenTeamTree.insert(id);
                }
                ps.frozenSubmitCount += 1;
                ps.hiddenSubs.push_back(sub);
            } else {
                // solved-before-freeze problems are still visible (no ranking effect after solved anyway)
                if (!ps.solved) {
                    needRankUpdate = true;
                }
            }
        } else {
            if (!team.problems[p].solved) needRankUpdate = true;
        }

        if (needRankUpdate) {
            rankingTree.erase(id);
            if (frozenTeamTree.find(id) != frozenTeamTree.end()) frozenTeamTree.erase(id);
            applyVisibleSubmission(team, p, st, tm);
            rankingTree.insert(id);
            if (!team.frozenProblems.empty()) frozenTeamTree.insert(id);
        }
    }

    void refreshLastFlushedRank() {
        int rk = 1;
        for (auto it = rankingTree.begin(); it != rankingTree.end(); ++it, ++rk) {
            lastFlushedRank[*it] = rk;
        }
    }

    static string problemDisplay(const Team &team, int p) {
        const ProblemState &ps = team.problems[p];
        if (ps.frozen) {
            if (ps.frozenWrongBase == 0) {
                return "0/" + to_string(ps.frozenSubmitCount);
            }
            return "-" + to_string(ps.frozenWrongBase) + "/" + to_string(ps.frozenSubmitCount);
        }
        if (ps.solved) {
            if (ps.wrong == 0) return "+";
            return "+" + to_string(ps.wrong);
        }
        if (ps.wrong == 0) return ".";
        return "-" + to_string(ps.wrong);
    }

    void printScoreboard() {
        int rk = 1;
        for (auto it = rankingTree.begin(); it != rankingTree.end(); ++it, ++rk) {
            const Team &team = g_teams[*it];
            cout << team.name << ' ' << rk << ' ' << team.solvedCount << ' ' << team.penalty;
            for (int p = 0; p < problemCount; ++p) {
                cout << ' ' << problemDisplay(team, p);
            }
            cout << '\n';
        }
    }

    void flush() {
        refreshLastFlushedRank();
        cout << "[Info]Flush scoreboard.\n";
    }

    void doFreeze() {
        if (frozen) {
            cout << "[Error]Freeze failed: scoreboard has been frozen.\n";
            return;
        }
        frozen = true;
        frozenTeamTree.clear();
        for (auto &team : g_teams) {
            team.frozenProblems.clear();
            for (int p = 0; p < problemCount; ++p) {
                ProblemState &ps = team.problems[p];
                ps.freezeEligible = !ps.solved;
                ps.frozen = false;
                ps.frozenWrongBase = ps.wrong;
                ps.frozenSubmitCount = 0;
                ps.hiddenSubs.clear();
            }
        }
        cout << "[Info]Freeze scoreboard.\n";
    }

    void doScroll() {
        if (!frozen) {
            cout << "[Error]Scroll failed: scoreboard has not been frozen.\n";
            return;
        }
        cout << "[Info]Scroll scoreboard.\n";

        refreshLastFlushedRank(); // flush before scrolling
        printScoreboard();

        while (!frozenTeamTree.empty()) {
            int id = *prev(frozenTeamTree.end()); // lowest-ranked frozen team
            Team &team = g_teams[id];
            int p = *team.frozenProblems.begin(); // smallest problem id
            ProblemState &ps = team.problems[p];

            int oldRank = (int)rankingTree.order_of_key(id);

            rankingTree.erase(id);
            frozenTeamTree.erase(id);

            for (const Submission &sub : ps.hiddenSubs) {
                applyVisibleSubmission(team, p, sub.status, sub.time);
            }

            ps.frozen = false;
            ps.frozenSubmitCount = 0;
            ps.hiddenSubs.clear();
            ps.freezeEligible = false;
            team.frozenProblems.erase(p);

            rankingTree.insert(id);
            if (!team.frozenProblems.empty()) {
                frozenTeamTree.insert(id);
            }

            int newRank = (int)rankingTree.order_of_key(id);
            if (newRank < oldRank) {
                auto itBelow = rankingTree.find_by_order(newRank + 1);
                if (itBelow != rankingTree.end()) {
                    const Team &team2 = g_teams[*itBelow];
                    cout << team.name << ' ' << team2.name << ' ' << team.solvedCount << ' ' << team.penalty << '\n';
                }
            }
        }

        frozen = false;
        for (auto &team : g_teams) {
            for (int p = 0; p < problemCount; ++p) {
                team.problems[p].freezeEligible = false;
                team.problems[p].frozen = false;
                team.problems[p].frozenSubmitCount = 0;
                team.problems[p].hiddenSubs.clear();
            }
            team.frozenProblems.clear();
        }

        printScoreboard();
        refreshLastFlushedRank();
    }

    void queryRanking(const string &teamName) {
        auto it = idByName.find(teamName);
        if (it == idByName.end()) {
            cout << "[Error]Query ranking failed: cannot find the team.\n";
            return;
        }
        int id = it->second;
        cout << "[Info]Complete query ranking.\n";
        if (frozen) {
            cout << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
        }
        cout << teamName << " NOW AT RANKING " << lastFlushedRank[id] << '\n';
    }

    void querySubmission(const string &teamName, const string &problemStr, const string &statusStr) {
        auto it = idByName.find(teamName);
        if (it == idByName.end()) {
            cout << "[Error]Query submission failed: cannot find the team.\n";
            return;
        }
        Team &team = g_teams[it->second];

        int pidx = (problemStr == "ALL") ? 26 : (problemStr[0] - 'A');
        int sidx;
        if (statusStr == "ALL") sidx = 4;
        else sidx = statusIndex(parseStatus(statusStr));

        cout << "[Info]Complete query submission.\n";
        int idx = team.lastIdx[pidx][sidx];
        if (idx < 0) {
            cout << "Cannot find any submission.\n";
            return;
        }
        const Submission &sub = team.submissions[idx];
        cout << team.name << ' ' << char('A' + sub.problem) << ' ' << statusToString(sub.status) << ' ' << sub.time << '\n';
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Contest contest;

    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        vector<string> tk;
        string w;
        while (ss >> w) tk.push_back(w);
        if (tk.empty()) continue;

        const string &cmd = tk[0];
        if (cmd == "ADDTEAM") {
            contest.addTeam(tk[1]);
        } else if (cmd == "START") {
            int duration = stoi(tk[2]);
            int problems = stoi(tk[4]);
            contest.start(duration, problems);
        } else if (cmd == "SUBMIT") {
            int p = tk[1][0] - 'A';
            const string &teamName = tk[3];
            JudgeStatus st = Contest::parseStatus(tk[5]);
            int tm = stoi(tk[7]);
            contest.submit(p, teamName, st, tm);
        } else if (cmd == "FLUSH") {
            contest.flush();
        } else if (cmd == "FREEZE") {
            contest.doFreeze();
        } else if (cmd == "SCROLL") {
            contest.doScroll();
        } else if (cmd == "QUERY_RANKING") {
            contest.queryRanking(tk[1]);
        } else if (cmd == "QUERY_SUBMISSION") {
            string teamName = tk[1];
            string prob = tk[3].substr(tk[3].find('=') + 1);
            string status = tk[5].substr(tk[5].find('=') + 1);
            contest.querySubmission(teamName, prob, status);
        } else if (cmd == "END") {
            cout << "[Info]Competition ends.\n";
            break;
        }
    }

    return 0;
}
