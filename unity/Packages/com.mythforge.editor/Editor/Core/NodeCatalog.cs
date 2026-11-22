using System;
using System.Collections.Generic;
using UnityEngine;

namespace MythForge.Editor
{
    public enum MfPropKind
    {
        String,
        Int,
        Float,
        Bool
    }

    public sealed class MfPropSpec
    {
        public string Name = string.Empty;
        public string Label = string.Empty;
        public MfPropKind Kind = MfPropKind.String;
        public bool Required;
        public MfJsonValue Default = MfJsonValue.Null();
        public long Min = long.MinValue;
        public long Max = long.MaxValue;
        public string[] Choices;

        public MfJsonValue CreateDefault()
        {
            return Default != null ? Default.Clone() : MfJsonValue.Null();
        }
    }

    public sealed class MfNodeTypeSpec
    {
        public string Id = string.Empty;
        public string Display = string.Empty;
        public string Category = string.Empty;
        public string Description = string.Empty;
        public Color Accent = new Color(0.25f, 0.55f, 0.85f);
        public int MinInputs;
        public int MaxInputs;
        public int MinOutputs;
        public int MaxOutputs;
        public string SearchKeys = string.Empty;
        public bool IsEntry;
        public bool IsExit;
        public List<MfPropSpec> Props = new List<MfPropSpec>();
        public Func<MfGraphNode, string> Summary;

        public MfPropSpec FindProp(string name)
        {
            for (int i = 0; i < Props.Count; i++)
            {
                if (Props[i].Name == name) return Props[i];
            }
            return null;
        }

        public string Summarize(MfGraphNode node)
        {
            if (Summary == null) return string.Empty;
            return Summary(node);
        }
    }

    /// <summary>
    /// A named set of node types. Quest, dialogue and skill editors each own one
    /// instance, so the same graph view/document code can drive every kind.
    /// </summary>
    public sealed class MfNodeCatalog
    {
        private readonly List<MfNodeTypeSpec> _types;

        public MfNodeCatalog(List<MfNodeTypeSpec> types)
        {
            _types = types ?? new List<MfNodeTypeSpec>();
        }

        public IReadOnlyList<MfNodeTypeSpec> All
        {
            get { return _types; }
        }

        public MfNodeTypeSpec Find(string typeId)
        {
            for (int i = 0; i < _types.Count; i++)
            {
                if (_types[i].Id == typeId) return _types[i];
            }
            return null;
        }

        public List<string> Categories()
        {
            var categories = new List<string>();
            for (int i = 0; i < _types.Count; i++)
            {
                if (!categories.Contains(_types[i].Category)) categories.Add(_types[i].Category);
            }
            return categories;
        }

        public MfNodeTypeSpec Entry
        {
            get
            {
                for (int i = 0; i < _types.Count; i++)
                {
                    if (_types[i].IsEntry) return _types[i];
                }
                return null;
            }
        }

        public MfNodeTypeSpec Exit
        {
            get
            {
                for (int i = 0; i < _types.Count; i++)
                {
                    if (_types[i].IsExit) return _types[i];
                }
                return null;
            }
        }
    }

    /// <summary>
    /// Mirror of schema/node_types.json for editor-side validation, menus and
    /// inspectors. The authoritative validation is always `mf check`; this
    /// catalog exists so the editor can give instant feedback while typing.
    /// </summary>
    public static class MfQuestNodeCatalog
    {
        public static readonly MfNodeCatalog Catalog = new MfNodeCatalog(new List<MfNodeTypeSpec>
        {
            new MfNodeTypeSpec
            {
                Id = "quest.start",
                Display = "任务开始",
                Category = "流程",
                Description = "任务入口，全图只能有一个。",
                Accent = new Color(0.20f, 0.62f, 0.36f),
                MinInputs = 0,
                MaxInputs = 0,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "start kaishi rukou entry",
                IsEntry = true,
                Summary = node => "开始"
            },
            new MfNodeTypeSpec
            {
                Id = "quest.end",
                Display = "任务结束",
                Category = "流程",
                Description = "任务出口，result 决定发出 QuestCompleted 还是 QuestFailed。",
                Accent = new Color(0.72f, 0.25f, 0.25f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 0,
                MaxOutputs = 0,
                SearchKeys = "end jieshu finish complete",
                IsExit = true,
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "result",
                        Label = "结果",
                        Kind = MfPropKind.String,
                        Default = MfJsonValue.String("success"),
                        Choices = new[] { "success", "failure" }
                    }
                },
                Summary = node =>
                {
                    MfJsonValue value;
                    string result = node.Props.TryGetValue("result", out value) ? value.AsString() : "success";
                    return result == "failure" ? "失败" : "成功";
                }
            },
            new MfNodeTypeSpec
            {
                Id = "quest.condition.kill",
                Display = "击杀条件",
                Category = "条件",
                Description = "等待指定目标被击杀 count 次。",
                Accent = new Color(0.25f, 0.48f, 0.78f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "kill jisha target di ren shas",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "target",
                        Label = "目标",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("Wolf")
                    },
                    new MfPropSpec
                    {
                        Name = "count",
                        Label = "数量",
                        Kind = MfPropKind.Int,
                        Default = MfJsonValue.Number(1),
                        Min = 1,
                        Max = 9999
                    }
                },
                Summary = node => string.Format("击杀 {0} ×{1}", PropString(node, "target", "目标"),
                    PropInt(node, "count", 1))
            },
            new MfNodeTypeSpec
            {
                Id = "quest.condition.collect",
                Display = "收集条件",
                Category = "条件",
                Description = "等待收集指定物品 count 个。",
                Accent = new Color(0.25f, 0.48f, 0.78f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "collect shouji item wupin",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "item",
                        Label = "物品",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("Wool")
                    },
                    new MfPropSpec
                    {
                        Name = "count",
                        Label = "数量",
                        Kind = MfPropKind.Int,
                        Default = MfJsonValue.Number(1),
                        Min = 1,
                        Max = 9999
                    }
                },
                Summary = node => string.Format("收集 {0} ×{1}", PropString(node, "item", "物品"),
                    PropInt(node, "count", 1))
            },
            new MfNodeTypeSpec
            {
                Id = "quest.condition.reach",
                Display = "到达区域",
                Category = "条件",
                Description = "玩家进入指定区域后继续。",
                Accent = new Color(0.25f, 0.48f, 0.78f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "reach daoda area quyu",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "area",
                        Label = "区域",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("Hilltop")
                    }
                },
                Summary = node => "到达 " + PropString(node, "area", "区域")
            },
            new MfNodeTypeSpec
            {
                Id = "quest.condition.talk",
                Display = "对话条件",
                Category = "条件",
                Description = "与指定 NPC 对话后继续。",
                Accent = new Color(0.25f, 0.48f, 0.78f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "talk duihua npc",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "npc",
                        Label = "NPC",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("Elder")
                    }
                },
                Summary = node => "对话 " + PropString(node, "npc", "NPC")
            },
            new MfNodeTypeSpec
            {
                Id = "quest.timer",
                Display = "计时器",
                Category = "流程",
                Description = "等待指定秒数。",
                Accent = new Color(0.45f, 0.45f, 0.55f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "timer jishiqi wait dengdai",
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
                Summary = node => string.Format("等待 {0} 秒", PropDouble(node, "seconds", 1.0))
            },
            new MfNodeTypeSpec
            {
                Id = "quest.log",
                Display = "日志",
                Category = "表现",
                Description = "发出 Log 事件，供 UI 显示提示。",
                Accent = new Color(0.40f, 0.40f, 0.46f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "log rizhi message tishi",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "text",
                        Label = "文本",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("提示")
                    }
                },
                Summary = node => "日志：" + PropString(node, "text", "文本")
            },
            new MfNodeTypeSpec
            {
                Id = "quest.reward",
                Display = "奖励",
                Category = "结算",
                Description = "调用 GrantReward(exp, item)。",
                Accent = new Color(0.72f, 0.58f, 0.20f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "reward jiangli exp item",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "exp",
                        Label = "经验",
                        Kind = MfPropKind.Int,
                        Default = MfJsonValue.Number(0),
                        Min = 0,
                        Max = 999999
                    },
                    new MfPropSpec
                    {
                        Name = "item",
                        Label = "物品",
                        Kind = MfPropKind.String,
                        Default = MfJsonValue.String("")
                    }
                },
                Summary = node => string.Format("奖励 {0} exp / {1}", PropInt(node, "exp", 0),
                    PropString(node, "item", "无"))
            },
            new MfNodeTypeSpec
            {
                Id = "quest.depends",
                Display = "前置任务",
                Category = "流程",
                Description = "声明构建依赖；运行时不产生指令。",
                Accent = new Color(0.50f, 0.35f, 0.65f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "depends qianzhi dependency",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "questId",
                        Label = "任务 ID",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("q_other")
                    }
                },
                Summary = node => "前置 " + PropString(node, "questId", "任务")
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

        public static List<string> Categories()
        {
            return Catalog.Categories();
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

        private static long PropInt(MfGraphNode node, string name, long fallback)
        {
            MfJsonValue value;
            return node.Props.TryGetValue(name, out value) && value.IsNumber ? value.AsInt() : fallback;
        }

        private static double PropDouble(MfGraphNode node, string name, double fallback)
        {
            MfJsonValue value;
            return node.Props.TryGetValue(name, out value) && value.IsNumber ? value.AsDouble() : fallback;
        }
    }
}
