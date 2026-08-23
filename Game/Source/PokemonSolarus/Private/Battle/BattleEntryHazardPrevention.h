#pragma once

namespace BattleEntryHazardPrevention
{
	struct FResult
	{
		bool bBypassesEntryHazards = false;
		bool bIndirectDamagePrevented = false;
	};

	/**
	 * Resolves the shared C08 precedence for entry-hazard prevention.
	 * A complete hazard bypass wins before an indirect-damage prevention hook.
	 */
	[[nodiscard]] inline FResult Resolve(
		const bool bBypassesEntryHazards,
		const bool bIndirectDamageWouldBePrevented)
	{
		FResult Result;
		Result.bBypassesEntryHazards = bBypassesEntryHazards;
		Result.bIndirectDamagePrevented =
			!bBypassesEntryHazards && bIndirectDamageWouldBePrevented;
		return Result;
	}
}
