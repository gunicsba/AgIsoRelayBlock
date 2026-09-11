# Patches against the vendored AgIsoStack++

Fixes we've had to make inside
[../components/AgIsoStack-plus-plus/upstream](../components/AgIsoStack-plus-plus/upstream),
which is a **pinned git submodule** — meaning edits there are *not*
captured by this repo's own commits and are silently lost on a fresh
`git submodule update --init --recursive`. Each patch here is the exact
diff, so it can be re-applied (and so there's a record of what was
changed and why, for upstreaming).

Apply them all after cloning:

```sh
cd firmware/components/AgIsoStack-plus-plus/upstream
git apply ../../../patches/*.patch
```

Check whether a patch is already applied with `git apply --check` (it
fails if it's already in), or just `git diff` inside the submodule.

---

## 0001-nm-keep-claim-state-when-binding-partner.patch

**Applies to submodule commit `795aa4981992cec4ce8ea23ca36a79d50f4f9244`.**

Fixes: *this ECU can never reconnect to a Virtual Terminal after its own
power cycle — only restarting the VT brings it back.*

Bench-reproduced 100% of the time: unplug the ECU, wait, plug it back in,
and it sits there forever with no VT. The serial log showed exactly this,
every boot:

```
I (4078) [NM]: A partner with name a0001d00afe00000 has claimed address 38 on channel 0.
I (4098) app_main: VT partner address claim: a Virtual Terminal has claimed an address
I (4598) [NM]: Control function with address 38 and NAME a0001d00afe00000 is now offline on channel 0.
I (4608) app_main: VT partner address claim: no Virtual Terminal on the bus
```

We discover the VT, then our *own* network manager declares it offline
520 ms later and never re-learns it, even though the VT is right there
broadcasting VT Status once a second the entire time.

### Root cause

`CANNetworkManager::update_new_partners()` binds a `PartneredControlFunction`
by copying `address`/`controlFunctionNAME` from the matching external CF
and then **replacing that CF in `controlFunctionTable` with the partner
object** — but it doesn't carry over
`claimedAddressSinceLastAddressClaimRequest`, so the partner lands in the
table with the default `false`.

Meanwhile `prune_inactive_control_functions()` drops every table entry
whose `claimedAddressSinceLastAddressClaimRequest` is `false`, 755 ms
after any *global Request For Address Claim* is seen on the bus.

On a fresh boot those two collide by construction: our own address-claim
procedure emits that global request, the VT answers it ~250 ms later, the
answer binds our partner (flag silently reset to `false`), and 755 ms
after the request the partner is pruned for "not having claimed" — when
the claim it just processed is the very reason the binding happened.

The kill is permanent, not transient, because pruning sets
`address = NULL_CAN_ADDRESS` while leaving `initialized == true`, and
`update_new_partners()` only ever (re-)binds partners whose `initialized`
is `false` — nothing anywhere resets that flag. So the partner object
stays dead for the life of the process. That's why restarting the *ECU*
doesn't help but restarting the *VT* does: a later claim arrives while a
table entry already exists, taking `update_address_table()`'s path that
does set the flag (`can_network_manager.cpp`, the `targetControlFunction
!= nullptr` branch), so it survives the next prune.

### The fix

Two hunks:

1. `update_new_partners()` — carry `claimedAddressSinceLastAddressClaimRequest`
   over from the CF being replaced. **This is the hunk that fixes the
   reproduced failure.**
2. The address-claim processing path — mark a CF as having claimed when
   we process its address claim. This covers the sibling case of a CF
   first *created* inside the prune window (`create_external_control_function()`
   leaves the flag at its default). Defensive; not separately reproduced.

Verified on the bench after the fix, same test, VT left running:

```
I (4129) app_main: VT partner address claim: a Virtual Terminal has claimed an address
I (5219) app_main: VT connection: CONNECTED
I (5219) vt_app: relay 1 -> OFF   ... (full display resync)
```

### Upstream status

Not yet reported upstream. Related but **not** the same as
[AgIsoStack-plus-plus#719](https://github.com/Open-Agriculture/AgIsoStack-plus-plus/pull/719)
(open), which adds a missing timeout to the VT client's
`WaitForPartnerVTStatusMessage` state. That's a real gap too, but it
would not fix this: after its timeout the client returns to
`Disconnected`, which re-checks `partnerControlFunction->get_address_valid()`
— still permanently false here — so it would just spin there instead,
with better logging. The two fixes are complementary: #719 makes the
client notice, this patch makes recovery actually possible.
