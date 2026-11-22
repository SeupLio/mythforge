using System;
using System.Collections.Generic;

namespace MythForge.Runtime
{
    /// <summary>
    /// The demo game's model (M11): one native VM per content kind, driven by
    /// simple player actions. Keeping this pure C# makes the whole loop testable
    /// in EditMode; the MonoBehaviour is only an IMGUI view over the session.
    /// </summary>
    public sealed class MfDemoSession : IDisposable
    {
        public readonly MfPlaySession Quest = new MfPlaySession();
        public readonly MfPlaySession Dialogue = new MfPlaySession();
        public readonly MfPlaySession Skill = new MfPlaySession();
        public readonly List<string> QuestLog = new List<string>();

        public string Error { get; private set; }
        public string CurrentDialogueLine { get; private set; }
        public bool DialogueEnded { get; private set; }
        public bool QuestCompleted { get; private set; }
        public bool SkillFinished { get; private set; }
        public string LastSkillEvent { get; private set; }
        private string _skillModule;

        public MfDemoSession()
        {
            Quest.LineAdded += OnQuestLine;
            Dialogue.LineAdded += OnDialogueLine;
            Skill.LineAdded += OnSkillLine;
        }

        public bool Load(string questModule, string dialogueModule, string skillModule)
        {
            Error = null;
            _skillModule = skillModule;
            string error;
            if (!Quest.Load(questModule, out error))
            {
                Error = "任务: " + error;
                return false;
            }
            if (!Dialogue.Load(dialogueModule, out error))
            {
                Error = "剧情: " + error;
                return false;
            }
            if (!Skill.Load(skillModule, out error))
            {
                Error = "技能: " + error;
                return false;
            }
            return true;
        }

        // --- player actions -------------------------------------------------
        public void KillWolf()
        {
            Quest.Emit("Kill", new Dictionary<string, object> { { "target", "Wolf" } });
        }

        public void TalkElder()
        {
            Quest.Emit("Talk", new Dictionary<string, object> { { "npc", "Elder" } });
        }

        public void ReachHilltop()
        {
            Quest.Emit("Reach", new Dictionary<string, object> { { "area", "Hilltop" } });
        }

        public void DialogueAdvance()
        {
            Dialogue.Emit("DialogueAdvance", null);
        }

        public void DialogueChoose()
        {
            Dialogue.Emit("DialogueChoiceSelected", null);
        }

        public void TimelineFinished(string asset)
        {
            Dialogue.Emit("TimelineFinished",
                new Dictionary<string, object> { { "asset", asset } });
        }

        public void CastSkill()
        {
            if (string.IsNullOrEmpty(_skillModule)) return;
            string error;
            if (!Skill.Load(_skillModule, out error))
            {
                Error = "技能: " + error;
                return;
            }
            SkillFinished = false;
            LastSkillEvent = null;
        }

        public void Advance(int deltaMilliseconds)
        {
            long micros = Math.Max(0, deltaMilliseconds) * 1000L;
            Quest.Advance(micros);
            Dialogue.Advance(micros);
            Skill.Advance(micros);
        }

        public string Snapshot()
        {
            string quest = QuestCompleted ? "已完成" : "进行中";
            string dialogue = DialogueEnded ? "已结束" : (CurrentDialogueLine ?? "等待开始");
            string skill = SkillFinished ? "已结束" : (LastSkillEvent ?? "等待施放");
            return string.Format("任务: {0}\n剧情: {1}\n技能: {2}", quest, dialogue, skill);
        }

        // --- trace handlers -------------------------------------------------
        private void OnQuestLine(MfTraceLine line)
        {
            if (line.Type != "emit") return;
            if (line.Name == "QuestStarted") QuestLog.Add("任务开始");
            else if (line.Name == "ObjectiveProgress") QuestLog.Add("目标进度 " + Join(line));
            else if (line.Name == "Log") QuestLog.Add(Join(line));
            else if (line.Name == "QuestCompleted")
            {
                QuestCompleted = true;
                QuestLog.Add("任务完成");
            }
            else if (line.Name == "QuestFailed") QuestLog.Add("任务失败");
        }

        private void OnDialogueLine(MfTraceLine line)
        {
            if (line.Type != "emit") return;
            if (line.Name == "DialogueLine")
            {
                string speaker = line.Args.Count > 0 ? Convert.ToString(line.Args[0]) : string.Empty;
                string text = line.Args.Count > 1 ? Convert.ToString(line.Args[1]) : string.Empty;
                CurrentDialogueLine = speaker.Length > 0 ? speaker + "：" + text : text;
            }
            else if (line.Name == "DialogueChoice")
            {
                CurrentDialogueLine = "选项：" + Join(line);
            }
            else if (line.Name == "DialogueEnded")
            {
                DialogueEnded = true;
                CurrentDialogueLine = "（剧情结束）";
            }
        }

        private void OnSkillLine(MfTraceLine line)
        {
            if (line.Type == "emit" && line.Name == "SkillFrame")
            {
                LastSkillEvent = Join(line);
            }
            else if (line.Type == "emit" && line.Name == "SkillEnded")
            {
                SkillFinished = true;
            }
        }

        private static string Join(MfTraceLine line)
        {
            return string.Join(" ", line.Args.ConvertAll(value =>
                value == null ? "nil" : Convert.ToString(value)).ToArray());
        }

        public void Dispose()
        {
            Quest.Dispose();
            Dialogue.Dispose();
            Skill.Dispose();
        }
    }
}
