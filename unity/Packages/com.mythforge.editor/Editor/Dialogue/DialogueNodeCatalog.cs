using System.Collections.Generic;
using UnityEngine;

namespace MythForge.Editor
{
    /// <summary>
    /// Editor mirror of the dialogue node types in schema/node_types.json.
    /// Node semantics: presentation nodes emit an event and then wait for the
    /// matching advance event; the compiler turns this into MythScript.
    /// </summary>
    public static class MfDialogueNodeCatalog
    {
        public static readonly MfNodeCatalog Catalog = new MfNodeCatalog(new List<MfNodeTypeSpec>
        {
            new MfNodeTypeSpec
            {
                Id = "dialogue.start",
                Display = "对话开始",
                Category = "流程",
                Description = "剧情入口，全图只能有一个。",
                Accent = new Color(0.22f, 0.58f, 0.52f),
                MinInputs = 0,
                MaxInputs = 0,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "start kaishi entry duihua",
                IsEntry = true,
                Summary = node => "开始"
            },
            new MfNodeTypeSpec
            {
                Id = "dialogue.line",
                Display = "台词",
                Category = "对白",
                Description = "播放一句台词，等待 DialogueAdvance 事件后继续。",
                Accent = new Color(0.25f, 0.50f, 0.82f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "line tai ci speaker text l10n",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "speaker",
                        Label = "说话人",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("Elder")
                    },
                    new MfPropSpec
                    {
                        Name = "text",
                        Label = "文本",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("……")
                    },
                    new MfPropSpec
                    {
                        Name = "l10nKey",
                        Label = "本地化 Key",
                        Kind = MfPropKind.String,
                        Default = MfJsonValue.String("")
                    }
                },
                Summary = node => PropString(node, "speaker", "说话人") + "：" +
                                  PropString(node, "text", "文本")
            },
            new MfNodeTypeSpec
            {
                Id = "dialogue.choice",
                Display = "选项",
                Category = "对白",
                Description = "展示选项（用 | 分隔），等待 DialogueChoiceSelected 事件。",
                Accent = new Color(0.55f, 0.42f, 0.80f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "choice xuanxiang option branch",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "speaker",
                        Label = "说话人",
                        Kind = MfPropKind.String,
                        Default = MfJsonValue.String("")
                    },
                    new MfPropSpec
                    {
                        Name = "options",
                        Label = "选项（| 分隔）",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("选项 A|选项 B")
                    }
                },
                Summary = node => "选项：" + PropString(node, "options", "选项")
            },
            new MfNodeTypeSpec
            {
                Id = "dialogue.flag",
                Display = "等待标记",
                Category = "条件",
                Description = "等待 DialogueFlag(flag == 名字) 事件后继续，用于剧情门控。",
                Accent = new Color(0.45f, 0.45f, 0.62f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "flag biaoji condition gate",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "flag",
                        Label = "标记名",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("met_elder")
                    }
                },
                Summary = node => "等待标记 " + PropString(node, "flag", "标记")
            },
            new MfNodeTypeSpec
            {
                Id = "dialogue.timeline",
                Display = "Timeline 演出",
                Category = "演出",
                Description = "调用 PlayTimeline(asset)；勾选等待时由 TimelineFinished 事件继续。",
                Accent = new Color(0.82f, 0.50f, 0.25f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "timeline yanchu playable cutscene",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "asset",
                        Label = "Timeline 资产",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("")
                    },
                    new MfPropSpec
                    {
                        Name = "waitForFinish",
                        Label = "等待播放完成",
                        Kind = MfPropKind.Bool,
                        Default = MfJsonValue.Bool(true)
                    }
                },
                Summary = node => "演出 " + ShortAsset(PropString(node, "asset", "未指定"))
            },
            new MfNodeTypeSpec
            {
                Id = "dialogue.wait",
                Display = "等待",
                Category = "流程",
                Description = "等待指定秒数，常用于给演出留时间。",
                Accent = new Color(0.42f, 0.42f, 0.50f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "wait dengdai timer delay",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "seconds",
                        Label = "秒数",
                        Kind = MfPropKind.Float,
                        Default = MfJsonValue.Number(1.0)
                    }
                },
                Summary = node => "等待 " + PropDouble(node, "seconds", 1.0) + " 秒"
            },
            new MfNodeTypeSpec
            {
                Id = "dialogue.end",
                Display = "对话结束",
                Category = "流程",
                Description = "剧情出口。",
                Accent = new Color(0.72f, 0.25f, 0.32f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 0,
                MaxOutputs = 0,
                SearchKeys = "end jieshu finish",
                IsExit = true,
                Summary = node => "结束"
            }
        });

        public static IReadOnlyList<MfNodeTypeSpec> All
        {
            get { return Catalog.All; }
        }

        public static MfNodeTypeSpec Find(string typeId)
        {
            return Catalog.Find(typeId);
        }

        private static string ShortAsset(string path)
        {
            if (string.IsNullOrEmpty(path)) return "未指定";
            int slash = path.LastIndexOf('/');
            return slash >= 0 ? path.Substring(slash + 1) : path;
        }

        private static string PropString(MfGraphNode node, string name, string fallback)
        {
            MfJsonValue value;
            if (node.Props.TryGetValue(name, out value) && value.IsString && value.AsString().Length > 0)
            {
                return value.AsString();
            }
            return fallback;
        }

        private static double PropDouble(MfGraphNode node, string name, double fallback)
        {
            MfJsonValue value;
            return node.Props.TryGetValue(name, out value) && value.IsNumber ? value.AsDouble() : fallback;
        }
    }
}
