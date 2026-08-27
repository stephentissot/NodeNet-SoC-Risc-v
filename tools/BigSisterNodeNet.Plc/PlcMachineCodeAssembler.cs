using System;
using System.Collections.Generic;
using System.IO;

namespace BigSisterNodeNet.Plc
{
    public sealed class PlcMachineCodeCompileException : Exception
    {
        public PlcMachineCodeCompileException(string message, int lineNumber)
            : base($"Line {lineNumber}: {message}")
        {
            LineNumber = lineNumber;
        }

        public int LineNumber { get; }
    }

    public static class PlcMachineCodeAssembler
    {
        public const byte HaltOpcode = 0x00;
        public const byte LoadPointBoolOpcode = 0x10;
        public const byte StorePointBoolOpcode = 0x11;

        public static PlcAssemblyResult Assemble(string source, PlcObjectFileOptions options)
        {
            if (source == null)
            {
                throw new ArgumentNullException(nameof(source));
            }
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            var output = new List<byte>();
            var result = new PlcAssemblyResult();
            var symbolIndexByName = new Dictionary<string, ushort>(StringComparer.Ordinal);
            using (var reader = new StringReader(source))
            {
                string line;
                var lineNumber = 0;
                while ((line = reader.ReadLine()) != null)
                {
                    lineNumber += 1;
                    var sanitized = StripComment(line).Trim();
                    if (sanitized.Length == 0)
                    {
                        continue;
                    }

                    var tokens = Tokenize(sanitized);
                    if (tokens.Count == 0)
                    {
                        continue;
                    }

                    var mnemonic = tokens[0].ToUpperInvariant();
                    switch (mnemonic)
                    {
                        case "CONST":
                            ParseConstPointDeclaration(tokens, lineNumber, result, symbolIndexByName);
                            break;

                        case "PARAM":
                            ParseParamPointDeclaration(tokens, lineNumber, result, symbolIndexByName, options);
                            break;

                        case "HALT":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(HaltOpcode);
                            break;

                        case "LOAD_BOOL":
                        case "LOAD_POINT_BOOL":
                        case "LB":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(LoadPointBoolOpcode);
                            WriteRelocatedSymbol(output, tokens[1], lineNumber, result, symbolIndexByName, PlcRuntimeLinkAccess.Read);
                            break;

                        case "STORE_BOOL":
                        case "STORE_POINT_BOOL":
                        case "SB":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(StorePointBoolOpcode);
                            WriteRelocatedSymbol(output, tokens[1], lineNumber, result, symbolIndexByName, PlcRuntimeLinkAccess.Write);
                            break;

                        case "DB":
                            if (tokens.Count < 2)
                            {
                                throw new PlcMachineCodeCompileException("DB requires at least one byte operand", lineNumber);
                            }

                            for (var index = 1; index < tokens.Count; index += 1)
                            {
                                output.Add(ParseByte(tokens[index], lineNumber));
                            }
                            break;

                        default:
                            throw new PlcMachineCodeCompileException($"Unsupported instruction '{tokens[0]}'", lineNumber);
                    }
                }
            }

            result.CodeBytes = output.ToArray();
            return result;
        }

        private static string StripComment(string line)
        {
            var semicolonIndex = line.IndexOf(';');
            var hashIndex = line.IndexOf('#');
            var commentIndex = -1;

            if (semicolonIndex >= 0 && hashIndex >= 0)
            {
                commentIndex = Math.Min(semicolonIndex, hashIndex);
            }
            else if (semicolonIndex >= 0)
            {
                commentIndex = semicolonIndex;
            }
            else if (hashIndex >= 0)
            {
                commentIndex = hashIndex;
            }

            return commentIndex >= 0 ? line.Substring(0, commentIndex) : line;
        }

        private static List<string> Tokenize(string line)
        {
            var tokens = new List<string>();
            foreach (var rawToken in line.Split(new[] { ' ', '\t', ',' }, StringSplitOptions.RemoveEmptyEntries))
            {
                tokens.Add(rawToken.Trim());
            }

            return tokens;
        }

        private static void RequireOperandCount(List<string> tokens, int expectedCount, int lineNumber)
        {
            if (tokens.Count != expectedCount)
            {
                throw new PlcMachineCodeCompileException($"Expected {expectedCount - 1} operand(s) for {tokens[0]}", lineNumber);
            }
        }

        private static void ParseConstPointDeclaration(List<string> tokens,
                                                       int lineNumber,
                                                       PlcAssemblyResult result,
                                                       IDictionary<string, ushort> symbolIndexByName)
        {
            if (tokens.Count != 4 || !string.Equals(tokens[1], "POINT_ID", StringComparison.OrdinalIgnoreCase))
            {
                throw new PlcMachineCodeCompileException("CONST syntax is 'CONST POINT_ID <symbol>, <deviceId.feature.pointId>'", lineNumber);
            }

            AddSymbol(result,
                      symbolIndexByName,
                      lineNumber,
                      tokens[2],
                      PlcObjectSymbolKind.ConstPointId,
                      tokens[3]);
        }

        private static void ParseParamPointDeclaration(List<string> tokens,
                                                       int lineNumber,
                                                       PlcAssemblyResult result,
                                                       IDictionary<string, ushort> symbolIndexByName,
                                                       PlcObjectFileOptions options)
        {
            if (tokens.Count != 3 || !string.Equals(tokens[1], "POINT_ID", StringComparison.OrdinalIgnoreCase))
            {
                throw new PlcMachineCodeCompileException("PARAM syntax is 'PARAM POINT_ID <symbol>'", lineNumber);
            }

            if (options.PointBindings == null || !options.PointBindings.TryGetValue(tokens[2], out var pointPath))
            {
                throw new PlcMachineCodeCompileException($"Missing point binding for PARAM '{tokens[2]}'", lineNumber);
            }

            AddSymbol(result,
                      symbolIndexByName,
                      lineNumber,
                      tokens[2],
                      PlcObjectSymbolKind.ParamPointId,
                      pointPath);
        }

        private static void AddSymbol(PlcAssemblyResult result,
                                      IDictionary<string, ushort> symbolIndexByName,
                                      int lineNumber,
                                      string symbolName,
                                      PlcObjectSymbolKind kind,
                                      string pointPath)
        {
            if (string.IsNullOrWhiteSpace(symbolName))
            {
                throw new PlcMachineCodeCompileException("Symbol name is required", lineNumber);
            }
            if (symbolIndexByName.ContainsKey(symbolName))
            {
                throw new PlcMachineCodeCompileException($"Symbol '{symbolName}' is already declared", lineNumber);
            }

            if (result.Symbols.Count >= ushort.MaxValue)
            {
                throw new PlcMachineCodeCompileException("Too many symbols", lineNumber);
            }

            symbolIndexByName[symbolName] = (ushort)result.Symbols.Count;
            result.Symbols.Add(new PlcAssemblySymbol
            {
                Name = symbolName,
                Kind = kind,
                PointPath = pointPath,
                ExpectedType = 0,
                Access = PlcRuntimeLinkAccess.Read,
            });
        }

        private static void WriteRelocatedSymbol(List<byte> output,
                                                 string symbolName,
                                                 int lineNumber,
                                                 PlcAssemblyResult result,
                                                 IDictionary<string, ushort> symbolIndexByName,
                                                 PlcRuntimeLinkAccess access)
        {
            if (!symbolIndexByName.TryGetValue(symbolName, out var symbolIndex))
            {
                throw new PlcMachineCodeCompileException($"Unknown symbol '{symbolName}'", lineNumber);
            }

            var symbol = result.Symbols[symbolIndex];
            symbol.Access = MergeAccess(symbol.Access, access);
            symbol.ExpectedType = 0;

            result.Relocations.Add(new PlcAssemblyRelocation
            {
                CodeOffset = (uint)output.Count,
                SymbolIndex = symbolIndex,
                RelocationKind = 0,
            });
            WriteUInt16(output, 0);
        }

        private static PlcRuntimeLinkAccess MergeAccess(PlcRuntimeLinkAccess current, PlcRuntimeLinkAccess next)
        {
            if (current == next)
            {
                return current;
            }

            return PlcRuntimeLinkAccess.ReadWrite;
        }

        private static byte ParseByte(string text, int lineNumber)
        {
            try
            {
                uint value;
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    value = Convert.ToUInt32(text.Substring(2), 16);
                }
                else
                {
                    value = Convert.ToUInt32(text);
                }

                if (value > byte.MaxValue)
                {
                    throw new PlcMachineCodeCompileException($"Value '{text}' is outside byte range", lineNumber);
                }

                return (byte)value;
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid byte literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid byte literal '{text}'", lineNumber);
            }
        }

        private static void WriteUInt16(List<byte> output, ushort value)
        {
            output.Add((byte)(value & 0xFFu));
            output.Add((byte)((value >> 8) & 0xFFu));
        }
    }
}