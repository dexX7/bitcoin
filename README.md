Bitcoin Core address-indexed
============================

[![Build Status](https://travis-ci.org/dexX7/bitcoin.svg?branch=addrindex-0.11)](https://travis-ci.org/dexX7/bitcoin)

Modifications
-------------

This branch is based on the [0.11](https://github.com/bitcoin/bitcoin/tree/0.11) ([diff](https://github.com/bitcoin/bitcoin/compare/0.11...dexX7:addrindex-0.11)) branch of Bitcoin Core and includes the required changes to allow extended transaction lookups based on an address.

The extensions are available for different bases:

- [Bitcoin Core 0.9](https://github.com/dexX7/bitcoin/tree/addrindex-0.9) ([diff](https://github.com/bitcoin/bitcoin/compare/0.9...dexX7:addrindex-0.9))
- [Bitcoin Core 0.10](https://github.com/dexX7/bitcoin/tree/addrindex-0.10) ([diff](https://github.com/bitcoin/bitcoin/compare/0.10...dexX7:addrindex-0.10))
- [Omni Core 0.0.9](https://github.com/dexX7/bitcoin/tree/addrindex-mscore-0.0.9) ([diff](https://github.com/mastercoin-MSC/mastercore/compare/mscore-0.0.9...dexX7:addrindex-mscore-0.0.9))
- [Omni Core 0.0.10](https://github.com/dexX7/bitcoin/tree/addrindex-omnicore-0.0.10) ([diff](https://github.com/OmniLayer/omnicore/compare/v0.0.10...dexX7:addrindex-omnicore-0.0.10))
- [Omni Core 0.0.11](https://github.com/dexX7/bitcoin/tree/addrindex-omnicore-0.0.11) ([diff](https://github.com/OmniLayer/omnicore/compare/v0.0.11...dexX7:addrindex-omnicore-0.0.11))

### Setup and configuration

Use `-addrindex=1` to enable address-based indexing of transactions.
Reindexing via `-reindex` is required the first time.

### RPC commands

The following new RPC commands are available:

```
> searchrawtransactions "address" (verbose skip count includeorphans)

Description:
Returns an array of all confirmed transactions associated with address.

Note: as per default, orphaned transactions, which are not part of the active chain, are
included in the results.

Arguments:
1. address          (string, required) The Bitcoin address
2. verbose          (numeric, optional, default=1) If 0, return only transaction hex
3. skip             (numeric, optional, default=0) The number of transactions to skip
4. count            (numeric, optional, default=100) The number of transactions to return
5. includeorphans   (numeric, optional, default=1) If 0, exclude orphaned transactions
```

```
> listallunspent "address" ( verbose minconf maxconf maxreqsigs )

Description:
Returns an array of confirmed, unspent transaction outputs with between minconf and maxconf
(inclusive) confirmations, spendable by the provided address, whereby maximal maxreqsigs
signatures are required to redeem the output.

Arguments:
1. address          (string, required) The Bitcoin address
2. verbose          (numeric, optional, default=0) If 0, exclude reqSigs, addresses, scriptPubKey (asm, hex), blockhash, blocktime, blockheight
3. minconf          (numeric, optional, default=1) The minimum confirmations to filter.
4. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter
5. maxreqsigs       (numeric, optional, default=1) The number of signatures required to spend the output
```

```
> getallbalance "address" ( minconf maxreqsigs )

Description:
Returns the sum of confirmed, spendable transaction outputs by address with at least minconf
confirmations, whereby maximal maxreqsigs signatures are allowed to be required to redeem an
output.

Arguments:
1. address          (string, required) The Bitcoin address
2. minconf          (numeric, optional, default=1) The minimum confirmations to filter
3. maxreqsigs       (numeric, optional, default=1) The number of signatures required to spend an output
```

```
> gettxposition "txid"

Description:
Returns information related to the position of transaction.

Arguments:
1. txid          (string, required) The transaction id

Result:
{
  "txid" : "hash",        (string) The transaction id (same as provided)
  "blockhash" : "hash",   (string) The block hash
  "blockheight" : n,      (numeric) The block height (if orphaned: -1, unconfirmed: 0)
  "position" : n          (numeric) The position of transaction within block (if unconfirmed: -1)
}
```

What is Bitcoin?
----------------

Bitcoin is an experimental new digital currency that enables instant payments to
anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. Bitcoin Core is the name of open source
software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the Bitcoin Core software, see https://www.bitcoin.org/en/download.

License
-------

Bitcoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see http://opensource.org/licenses/MIT.

Development process
-------------------

Developers work in their own trees, then submit pull requests when they think
their feature or bug fix is ready.

If it is a simple/trivial/non-controversial change, then one of the Bitcoin
development team members simply pulls it.

If it is a *more complicated or potentially controversial* change, then the patch
submitter will be asked to start a discussion (if they haven't already) on the
[mailing list](https://lists.linuxfoundation.org/mailman/listinfo/bitcoin-dev)

The patch will be accepted if there is broad consensus that it is a good thing.
Developers should expect to rework and resubmit patches if the code doesn't
match the project's coding conventions (see [doc/developer-notes.md](doc/developer-notes.md)) or are
controversial.

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/bitcoin/bitcoin/tags) are created
regularly to indicate new official, stable release versions of Bitcoin.

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write unit tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run (assuming they weren't disabled in configure) with: `make check`

Every pull request is built for both Windows and Linux on a dedicated server,
and unit and sanity tests are automatically run. The binaries produced may be
used for manual QA testing — a link to them will appear in a comment on the
pull request posted by [BitcoinPullTester](https://github.com/BitcoinPullTester). See https://github.com/TheBlueMatt/test-scripts
for the build/test scripts.

### Manual Quality Assurance (QA) Testing

Large changes should have a test plan, and should be tested by somebody other
than the developer who wrote the code.
See https://github.com/bitcoin/QA/ for how to create a test plan.

Translations
------------

Changes to translations as well as new translations can be submitted to
[Bitcoin Core's Transifex page](https://www.transifex.com/projects/p/bitcoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.

Translators should also subscribe to the [mailing list](https://groups.google.com/forum/#!forum/bitcoin-translators).
