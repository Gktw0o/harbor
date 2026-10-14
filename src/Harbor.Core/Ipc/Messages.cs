namespace Harbor.Core.Ipc;

/// <summary>A control-channel message. The wire format is defined by contracts/*.schema.json.</summary>
public abstract record Message;

/// <summary>Liveness probe, Harbor.Shell to dock (contracts/ping.schema.json, v1).</summary>
public sealed record Ping(uint Seq) : Message;

/// <summary>Answer to a <see cref="Ping"/>, dock to Harbor.Shell, echoing its seq (contracts/pong.schema.json, v1).</summary>
public sealed record Pong(uint Seq) : Message;
