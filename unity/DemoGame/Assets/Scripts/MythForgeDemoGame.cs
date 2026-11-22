using System.IO;
using MythForge.Runtime;
using UnityEngine;

/// <summary>
/// M11 demo: a tiny IMGUI scene that drives three content modules through the
/// native SDK. It shows quest progress, dialogue lines and a time-driven skill
/// in one place; the heavy lifting stays in the C++ VM.
/// </summary>
public sealed class MythForgeDemoGame : MonoBehaviour
{
    private const string TimelineAsset =
        "Assets/MythForgeContent/Timelines/TL_ElderIntro.playable";

    public string contentFolder = "MythForge";

    private MfDemoSession _session;
    private string _error;
    private Vector2 _logScroll;

    private void Start()
    {
        Reload();
    }

    private void OnDestroy()
    {
        if (_session != null) _session.Dispose();
    }

    private void Update()
    {
        if (_session != null) _session.Advance(Mathf.RoundToInt(Time.deltaTime * 1000f));
    }

    public void Reload()
    {
        if (_session != null)
        {
            _session.Dispose();
            _session = null;
        }
        string directory = Path.Combine(Application.streamingAssetsPath, contentFolder);
        string quest = Path.Combine(directory, "q_forest_wolves.mfb");
        string dialogue = Path.Combine(directory, "d_elder_intro.mfb");
        string skill = Path.Combine(directory, "sk_wolf_bite.mfb");
        if (!File.Exists(quest) || !File.Exists(dialogue) || !File.Exists(skill))
        {
            _error = "缺少运行时内容。请先执行菜单 MythForge/同步运行时内容到 StreamingAssets。\n" +
                     "目录: " + directory;
            return;
        }
        var session = new MfDemoSession();
        if (!session.Load(quest, dialogue, skill))
        {
            _error = session.Error;
            session.Dispose();
            return;
        }
        _session = session;
        _error = null;
    }

    private void OnGUI()
    {
        GUILayout.BeginArea(new Rect(16f, 16f, 430f, 560f), GUI.skin.box);
        GUILayout.Label("MythForge Demo — 任务 / 剧情 / 技能");
        GUILayout.Label("SDK: " + MfNativeLibrary.Version());

        if (_error != null)
        {
            GUILayout.Label(_error);
            if (GUILayout.Button("重试")) Reload();
            GUILayout.EndArea();
            return;
        }
        if (_session == null)
        {
            GUILayout.EndArea();
            return;
        }

        GUILayout.Label("任务");
        GUILayout.BeginHorizontal();
        if (GUILayout.Button("击杀狼")) _session.KillWolf();
        if (GUILayout.Button("与长老对话")) _session.TalkElder();
        if (GUILayout.Button("到达山顶")) _session.ReachHilltop();
        GUILayout.EndHorizontal();

        GUILayout.Label("剧情");
        GUILayout.BeginHorizontal();
        if (GUILayout.Button("下一句")) _session.DialogueAdvance();
        if (GUILayout.Button("选择选项")) _session.DialogueChoose();
        if (GUILayout.Button("Timeline 完成")) _session.TimelineFinished(TimelineAsset);
        GUILayout.EndHorizontal();

        GUILayout.Label("技能");
        GUILayout.BeginHorizontal();
        if (GUILayout.Button("施放撕咬")) _session.CastSkill();
        if (GUILayout.Button("快进 500ms")) _session.Advance(500);
        GUILayout.EndHorizontal();

        GUILayout.Space(6f);
        GUILayout.Label(_session.Snapshot());
        if (!string.IsNullOrEmpty(_session.Error)) GUILayout.Label("错误: " + _session.Error);

        GUILayout.Label("任务日志");
        _logScroll = GUILayout.BeginScrollView(_logScroll, GUILayout.Height(140f));
        for (int i = 0; i < _session.QuestLog.Count; i++)
        {
            GUILayout.Label(_session.QuestLog[i]);
        }
        GUILayout.EndScrollView();

        if (GUILayout.Button("重置全部")) Reload();
        GUILayout.EndArea();
    }
}
