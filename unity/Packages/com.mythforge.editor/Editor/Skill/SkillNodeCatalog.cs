using System.Collections.Generic;
using UnityEngine;

namespace MythForge.Editor
{
    /// <summary>
    /// Editor mirror of the skill node types in schema/node_types.json.
    /// A skill is a time-ordered chain of frames; the compiler turns frame times
    /// into relative `wait time` instructions and host SkillEvent calls.
    /// </summary>
    public static class MfSkillNodeCatalog
    {
        public const string FrameType = "skill.frame";

        public static readonly MfNodeCatalog Catalog = new MfNodeCatalog(new List<MfNodeTypeSpec>
        {
            new MfNodeTypeSpec
            {
                Id = "skill.cast",
                Display = "施放入口",
                Category = "流程",
                Description = "技能入口，全图只能有一个。",
                Accent = new Color(0.20f, 0.62f, 0.45f),
                MinInputs = 0,
                MaxInputs = 0,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "cast start shifang entry",
                IsEntry = true,
                Summary = node => "施放"
            },
            new MfNodeTypeSpec
            {
                Id = FrameType,
                Display = "技能帧",
                Category = "时间轴",
                Description = "在 timeMs 时刻触发一次事件（damage/buff/vfx/sfx/move）。",
                Accent = new Color(0.28f, 0.52f, 0.86f),
                MinInputs = 1,
                MaxInputs = 1,
                MinOutputs = 1,
                MaxOutputs = 1,
                SearchKeys = "frame zhen event damage buff vfx sfx move",
                Props =
                {
                    new MfPropSpec
                    {
                        Name = "timeMs",
                        Label = "时间 (ms)",
                        Kind = MfPropKind.Int,
                        Required = true,
                        Default = MfJsonValue.Number(0),
                        Min = 0,
                        Max = 600000
                    },
                    new MfPropSpec
                    {
                        Name = "event",
                        Label = "事件",
                        Kind = MfPropKind.String,
                        Required = true,
                        Default = MfJsonValue.String("damage"),
                        Choices = new[] { "damage", "buff", "vfx", "sfx", "move" }
                    },
                    new MfPropSpec
                    {
                        Name = "target",
                        Label = "目标",
                        Kind = MfPropKind.String,
                        Default = MfJsonValue.String("")
                    },
                    new MfPropSpec
                    {
                        Name = "amount",
                        Label = "数值",
                        Kind = MfPropKind.Int,
                        Default = MfJsonValue.Number(0),
                        Min = -999999,
                        Max = 999999
                    },
                    new MfPropSpec
                    {
                        Name = "asset",
                        Label = "资源",
                        Kind = MfPropKind.String,
                        Default = MfJsonValue.String("")
                    }
                },
                Summary = node => PropInt(node, "timeMs", 0) + "ms " +
                                  PropString(node, "event", "event") + " " +
                                  PropString(node, "asset", PropString(node, "target", ""))
            },
            new MfNodeTypeSpec
            {
                Id = "skill.end",
                Display = "技能结束",
                Category = "流程",
                Description = "技能出口。",
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
    }
}
