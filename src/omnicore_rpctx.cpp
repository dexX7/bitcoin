// RPC calls for creating and sending Omni transactions

#include "omnicore_rpctx.h"

#include "mastercore.h"
#include "mastercore_convert.h"
#include "mastercore_errors.h"
#include "mastercore_parse_string.h"
#include "mastercore_rpc_values.h"
#include "mastercore_sp.h"
#include "mastercore_dex.h"
#include "mastercore_tx.h"
#include "omnicore_createpayload.h"
#include "omnicore_pending.h"

#include "rpcserver.h"
#include "wallet.h"

#include <boost/algorithm/string.hpp>
#include <boost/exception/to_string.hpp>
#include <boost/lexical_cast.hpp>

#include "json/json_spirit_value.h"

#include <stdint.h>

#include <map>
#include <stdexcept>
#include <string>

using boost::algorithm::token_compress_on;
using boost::to_string;

using std::map;
using std::runtime_error;
using std::string;
using std::vector;

using namespace json_spirit;
using namespace mastercore;

static void RequireSaneReferenceAmount(int64_t amount)
{
    if ((0.01 * COIN) < amount) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid reference amount");
    }
}

static void RequireSufficientBalance(const std::string fromAddress, uint32_t propertyId, int64_t amount)
{
    int64_t balance = getMPbalance(fromAddress, propertyId, BALANCE);
    if (balance < amount) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Sender has insufficient balance");
    }

    int64_t balanceUnconfirmed = getUserAvailableMPbalance(fromAddress, propertyId, BALANCE);
    if (balanceUnconfirmed < amount) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Sender has insufficient balance (due to pending transactions)");
    }
}

static void RequireNonEmptyPropertyName(const std::string& name)
{
    if (name.empty()) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Property name must not be empty");
    }
}

static void RequireOnlyMSC(uint32_t propertyId)
{
    if (propertyId < 1 || 2 < propertyId) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid propertyID for sale - only 1 and 2 are permitted");
    }
}

static void RequireActiveCrowdsale(uint32_t propertyId)
{
    if (!isCrowdsaleActive(propertyId)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "The specified property does not have a crowdsale active");
    }
}

static void RequireTokenAdministrator(const std::string& sender, uint32_t propertyId)
{
    CMPSPInfo::Entry sp;
    mastercore::_my_sps->getSP(propertyId, sp);

    if (sender != sp.issuer) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Sender is not authorized to manage this property");
    }
}

// send_OMNI - simple send
Value send_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 4 || params.size() > 6)
        throw runtime_error(
            "send_OMNI \"fromaddress\" \"toaddress\" propertyid \"amount\" ( \"redeemaddress\" \"referenceamount\" )\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "ToAddress     : the address to send to\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount        : the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem class B data outputs. Defaults to FromAddress\n"
            "ReferenceAmount:(optional) the number of satoshis to send to the recipient in the reference output\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored send_OMNI 1FromAddress 1ToAddress PropertyID Amount\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    std::string toAddress = ParseAddress(params[1]);

    CMPSPInfo::Entry sp;
    uint32_t propertyId = ParsePropertyId(params[2], sp);

    int64_t amount = ParseAmount(params[3], sp.isDivisible());
    std::string redeemAddress = (params.size() > 4) ? ParseAddress(params[4]): "";

    int64_t referenceAmount = 0;
    if (params.size() > 5) {
        referenceAmount = ParseAmount(params[5], true);
    }

    // perform checks
    RequireSaneReferenceAmount(referenceAmount);
    RequireSufficientBalance(fromAddress, propertyId, amount);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_SimpleSend(propertyId, amount);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, toAddress, redeemAddress, referenceAmount, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            PendingAdd(txid, fromAddress, toAddress, MSC_TYPE_SIMPLE_SEND, propertyId, amount);
            return txid.GetHex();
        }
    }
}

// senddexsell_OMNI - DEx sell offer
Value senddexsell_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 7)
        throw runtime_error(
            "senddexsell_OMNI \"fromaddress\" propertyidforsale \"amountforsale\" \"amountdesired\" paymentwindow minacceptfee action\n"
            "\nPlace or cancel a sell offer on the BTC/MSC layer of the distributed exchange.\n"
            "\nParameters:\n"
            "FromAddress         : the address to send this transaction from\n"
            "PropertyIDForSale   : the property to list for sale (must be MSC or TMSC)\n"
            "AmountForSale       : the amount to list for sale\n"
            "AmountDesired       : the amount of BTC desired\n"
            "PaymentWindow       : the time limit a buyer has to pay following a successful accept\n"
            "MinAcceptFee        : the mining fee a buyer has to pay to accept\n"
            "Action              : the action to take: (1) new, (2) update, (3) cancel \n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored senddexsell_OMNI \"1FromAddress\" PropertyIDForSale \"AmountForSale\" \"AmountDesired\" PaymentWindow MinAcceptFee Action\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint32_t propertyIdForSale = ParsePropertyId(params[1]);
    string strAmountForSale = params[2].get_str();
    string strAmountDesired = params[3].get_str();
    uint8_t paymentWindow = ParsePaymentTimeframe(params[4]);
    int64_t minAcceptFee = ParseCommitmentFee(params[5]);
    uint8_t action = ParseDexAction(params[6]);

    // perform conversions    
    int64_t amountForSale = 0, amountDesired = 0;
    amountForSale = StrToInt64(strAmountForSale, true); // TMSC/MSC always divisible
    amountDesired = StrToInt64(strAmountDesired, true); // BTC so always divisible

    // perform checks
    RequireOnlyMSC(propertyIdForSale);
    
    if (action <= 2) { // actions 3 permit zero values, skip check
        if (0 >= amountForSale) throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for sale");
        if (!isRangeOK(amountForSale)) throw JSONRPCError(RPC_TYPE_ERROR, "Amount for sale not in range");
        if (0 >= amountDesired) throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount desired");
        if (!isRangeOK(amountDesired)) throw JSONRPCError(RPC_TYPE_ERROR, "Amount desired not in range");
    }
    if (action != 3) { // only check for sufficient balance for new/update sell offers
        RequireSufficientBalance(fromAddress, propertyIdForSale, amountForSale);
    }
    if ((action == 1) && (DEx_offerExists(fromAddress, propertyIdForSale))) throw JSONRPCError(RPC_TYPE_ERROR, "There is already a sell offer from this address on the distributed exchange, use update instead");

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_DExSell(propertyIdForSale, amountForSale, amountDesired, paymentWindow, minAcceptFee, action);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            PendingAdd(txid, fromAddress, "", MSC_TYPE_TRADE_OFFER, propertyIdForSale, amountForSale, 0, amountDesired, action);
            return txid.GetHex();
        }
    }
}

// senddexaccept_OMNI - DEx accept offer
Value senddexaccept_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "senddexaccept_OMNI \"fromaddress\" \"toaddress\" propertyid \"amount\"\n"
            "\nCreates and broadcasts an accept offer for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "ToAddress     : the address to send the accept to\n"
            "PropertyID    : the id of the property to accept\n"
            "Amount        : the amount to accept\n"
            "Override      : override minimum accept fee and payment window checks (use with caution!)\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored senddexaccept_OMNI 1FromAddress 1ToAddress PropertyID Amount\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    std::string toAddress = ParseAddress(params[1]);
    uint32_t propertyId = ParsePropertyId(params[2]);
    int64_t amount = ParseAmount(params[2], true); // MSC/TMSC always divisible
    bool override = false;
    if (params.size() > 4) override = params[4].get_bool();

    // perform checks
    RequireOnlyMSC(propertyId);
    if (!DEx_offerExists(toAddress, propertyId)) throw JSONRPCError(RPC_TYPE_ERROR, "There is no matching sell offer on the distributed exchange");

    // retrieve the sell we're accepting and obtain the required minimum fee and payment window
    CMPOffer *sellOffer = DEx_getOffer(toAddress, propertyId);
    if (sellOffer == NULL) throw JSONRPCError(RPC_TYPE_ERROR, "Unable to load sell offer from the distributed exchange");
    int64_t nMinimumAcceptFee = sellOffer->getMinFee();
    unsigned char nBlockTimeLimit = sellOffer->getBlockTimeLimit();

    if (!override) { // reject unsafe accepts - note client maximum tx fee will always be respected regardless of override here
        if (nMinimumAcceptFee > 1000000) throw JSONRPCError(RPC_TYPE_ERROR, "Unsafe trade protection - minimum accept fee is above 0.01 BTC");
        if (nBlockTimeLimit < 10) throw JSONRPCError(RPC_TYPE_ERROR, "Unsafe trade protection - payment time limit is less than 10 blocks");
    }

    // use new 0.10 custom fee to set the accept minimum fee appropriately
    CFeeRate payTxFeeOriginal = payTxFee;
    bool fPayAtLeastCustomFeeOriginal = fPayAtLeastCustomFee;
    payTxFee = CFeeRate(nMinimumAcceptFee, 1000);
    fPayAtLeastCustomFee = true;

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_DExAccept(propertyId, amount);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, toAddress, "", 0, payload, txid, rawHex, autoCommit);

    // set the custom fee back to original
    payTxFee = payTxFeeOriginal;
    fPayAtLeastCustomFee = fPayAtLeastCustomFeeOriginal;

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendissuancecrowdsale_OMNI - Issue new property with crowdsale
Value sendissuancecrowdsale_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 14)
        throw runtime_error(
            "sendissuancecrowdsale_OMNI \"fromaddress\" ecosystem type previousid \"category\" \"subcategory\" \"name\" \"url\" \"data\" propertyiddesired tokensperunit deadline earlybonus issuerpercentage\n"
            "\nCreates and broadcasts a property creation transaction (crowdsale issuance) with the supplied details.\n"
            "\nParameters:\n"
            "FromAddress        : the address to send from\n"
            "Ecosystem          : the ecosystem to create the property - (1) main, (2) test\n"
            "Type               : the type of tokens - (1) indivisible, (2) divisible\n"
            "PreviousID         : the previous property id (0 for a new property)\n"
            "Category           : The category for the new property (max 255 chars)\n"
            "Subcategory        : the subcategory for the new property (max 255 chars)\n"
            "Name               : the name of the new property (max 255 chars)\n"
            "URL                : the URL for the new property (max 255 chars)\n"
            "Data               : additional data for the new property (max 255 chars)\n"
            "PropertyIDDesired  : the property that will be used to purchase from the crowdsale\n"
            "TokensPerUnit      : the amount of tokens per unit crowdfunded\n"
            "Deadline           : the deadline for the crowdsale\n"
            "EarlyBonus         : the early bonus %/week\n"
            "IssuerPercentage   : the percentage of crowdfunded tokens that will be additionally created for the issuer\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendissuancecrowdsale_OMNI \"1FromAddress\" Ecosystem Type PreviousID \"Category\" \"Subcategory\" \"Name\" \"URL\" \"Data\" PropertyIDDesired TokensPerUnit Deadline EarlyBonus IssuerPercentage\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint8_t ecosystem = ParseEcosystem(params[1]);
    uint16_t type = ParsePropertyType(params[2]);
    uint32_t previousId = ParsePreviousPropertyId(params[3]);
    std::string category = ParseText(params[4]);
    std::string subcategory = ParseText(params[5]);
    std::string name = ParseText(params[6]);
    std::string url = ParseText(params[7]);
    std::string data = ParseText(params[8]);
    uint32_t propertyIdDesired = ParsePropertyId(params[9]);
    int64_t numTokens = ParseAmount(params[10], type);
    int64_t deadline = ParseDeadline(params[11]);
    int64_t earlyBonus = ParseEarlyBirdBonus(params[12]);
    int64_t issuerPercentage = ParseIssuerBonus(params[13]);

    // perform checks
    RequireNonEmptyPropertyName(name);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_IssuanceVariable(ecosystem, type, previousId, category, subcategory, name, url, data, propertyIdDesired, numTokens, deadline, earlyBonus, issuerPercentage);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendissuancecfixed_OMNI - Issue new property with fixed amount
Value sendissuancefixed_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 10)
        throw runtime_error(
            "sendissuancefixed_OMNI \"fromaddress\" ecosystem type previousid \"category\" \"subcategory\" \"name\" \"url\" \"data\" \"amount\"\n"
            "\nCreates and broadcasts a property creation transaction (fixed issuance) with the supplied details.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "Ecosystem     : the ecosystem to create the property - (1) main, (2) test\n"
            "Type          : the type of tokens - (1) indivisible, (2) divisible\n"
            "PreviousID    : the previous property id (0 for a new property)\n"
            "Category      : The category for the new property (max 255 chars)\n"
            "Subcategory   : the subcategory for the new property (max 255 chars)\n"
            "Name          : the name of the new property (max 255 chars)\n"
            "URL           : the URL for the new property (max 255 chars)\n"
            "Data          : additional data for the new property (max 255 chars)\n"
            "Amount        : the number of tokens to create\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendissuancefixed_OMNI \"1FromAddress\" Ecosystem Type PreviousID \"Category\" \"Subcategory\" \"Name\" \"URL\" \"Data\" \"Amount\"\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint8_t ecosystem = ParseEcosystem(params[1]);
    uint16_t type = ParsePropertyType(params[2]);
    uint32_t previousId = ParsePreviousPropertyId(params[3]);
    std::string category = ParseText(params[4]);
    std::string subcategory = ParseText(params[5]);
    std::string name = ParseText(params[6]);
    std::string url = ParseText(params[7]);
    std::string data = ParseText(params[8]);
    int64_t amount = ParseAmount(params[9], type);

    // perform checks
    RequireNonEmptyPropertyName(name);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_IssuanceFixed(ecosystem, type, previousId, category, subcategory, name, url, data, amount);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendissuancemanual_OMNI - Issue new property with manual issuance (grant/revoke)
Value sendissuancemanaged_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 9)
        throw runtime_error(
            "sendissuancemanual_OMNI \"fromaddress\" ecosystem type previousid \"category\" \"subcategory\" \"name\" \"url\" \"data\"\n"
            "\nCreates and broadcasts a property creation transaction (managed issuance) with the supplied details.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "Ecosystem     : the ecosystem to create the property - (1) main, (2) test\n"
            "Type          : the type of tokens - (1) indivisible, (2) divisible\n"
            "PreviousID    : the previous property id (0 for a new property)\n"
            "Category      : The category for the new property (max 255 chars)\n"
            "Subcategory   : the subcategory for the new property (max 255 chars)\n"
            "Name          : the name of the new property (max 255 chars)\n"
            "URL           : the URL for the new property (max 255 chars)\n"
            "Data          : additional data for the new property (max 255 chars)\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendissuancemanual_OMNI \"1FromAddress\" Ecosystem Type PreviousID \"Category\" \"Subcategory\" \"Name\" \"URL\" \"Data\"\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint8_t ecosystem = ParseEcosystem(params[1]);
    uint16_t type = ParsePropertyType(params[2]);
    uint32_t previousId = ParsePreviousPropertyId(params[3]);
    std::string category = ParseText(params[4]);
    std::string subcategory = ParseText(params[5]);
    std::string name = ParseText(params[6]);
    std::string url = ParseText(params[7]);
    std::string data = ParseText(params[8]);

    // perform checks
    RequireNonEmptyPropertyName(name);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_IssuanceManaged(ecosystem, type, previousId, category, subcategory, name, url, data);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendsto_OMNI - Send to owners
Value sendsto_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendsto_OMNI \"fromaddress\" propertyid \"amount\" ( \"redeemaddress\" )\n"
            "\nCreates and broadcasts a send-to-owners transaction for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount (string): the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem class B data outputs. Defaults to FromAddress\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendsto_OMNI 1FromAddress PropertyID Amount\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint32_t propertyId = ParsePropertyId(params[1]);
    int64_t amount = ParseAmount(params[2], isPropertyDivisible(propertyId));
    std::string redeemAddress = (params.size() > 3) ? (params[3].get_str()): "";

    // perform checks
    RequireSufficientBalance(fromAddress, propertyId, amount);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_SendToOwners(propertyId, amount);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", redeemAddress, 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            PendingAdd(txid, fromAddress, "", MSC_TYPE_SEND_TO_OWNERS, propertyId, amount);
            return txid.GetHex();
        }
    }
}

// sendgrant_OMNI - Grant tokens
Value sendgrant_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "sendgrant_OMNI \"fromaddress\" \"toaddress\" propertyid \"amount\" ( \"memo\" )\n"
            "\nCreates and broadcasts a token grant for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send this transaction from\n"
            "ToAddress     : the address to send the granted tokens to (defaults to FromAddress)\n"
            "PropertyID    : the id of the smart property to grant\n"
            "Amount        : the amount to grant\n"
            "Memo          : (optional) attach a text note to this transaction (max 255 chars)\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendgrant_OMNI \"1FromAddress\" \"1ToAddress\" PropertyID Amount\n"
            ">omnicored sendgrant_OMNI \"1FromAddress\" \"\" PropertyID Amount \"Grant tokens to the sending address and attach this note\"\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0].get_str());
    std::string toAddress = fromAddress;
    if (!params[1].get_str().empty()) {
        toAddress = ParseAddress(params[1]);
    }
    CMPSPInfo::Entry sp;
    uint32_t propertyId = ParsePropertyId(params[2], sp);
    int64_t amount = ParseAmount(params[3], sp.isDivisible());
    std::string memo = (params.size() > 4) ? ParseText(params[4]): "";

    // perform checks
    RequireTokenAdministrator(fromAddress, propertyId);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_Grant(propertyId, amount, memo);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, toAddress, "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendrevoke_OMNI - Revoke tokens
Value sendrevoke_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendrevoke_OMNI \"fromaddress\" propertyid \"amount\" ( \"memo\" )\n"
            "\nCreates and broadcasts a token revoke for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send the transaction from\n"
            "PropertyID    : the id of the smart property to revoke\n"
            "Amount        : the amount to revoke\n"
            "Memo          : (optional) attach a text note to this transaction (max 255 chars)\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendrevoke_OMNI \"1FromAddress\" PropertyID Amount\n"
            ">omnicored sendrevoke_OMNI \"1FromAddress\" PropertyID Amount \"Revoke tokens from the sending address and attach this note\"\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    CMPSPInfo::Entry sp;
    uint32_t propertyId = ParsePropertyId(params[1], sp);
    int64_t amount = ParseAmount(params[2], sp.isDivisible());
    std::string memo = (params.size() > 3) ? ParseText(params[3]): "";

    // perform checks
    RequireTokenAdministrator(fromAddress, propertyId);
    RequireSufficientBalance(fromAddress, propertyId, amount);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_Revoke(propertyId, amount, memo);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendclosecrowdsale_OMNI - Close an active crowdsale
Value sendclosecrowdsale_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "sendclosecrowdsale_OMNI \"fromaddress\" propertyid\n"
            "\nCreates and broadcasts a close crowdsale message for a given currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send this transaction from\n"
            "PropertyID    : the id of the smart property to close the crowdsale\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendclosecrowdsale_OMNI \"1FromAddress\" PropertyID\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint32_t propertyId = ParsePropertyId(params[1]);

    // perform checks
    RequireActiveCrowdsale(propertyId);
    RequireTokenAdministrator(fromAddress, propertyId);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_CloseCrowdsale(propertyId);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}

// sendtrade_OMNI - MetaDEx trade
Value sendtrade_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw runtime_error(
            "sendtrade_OMNI \"fromaddress\" propertyidforsale \"amountforsale\" propertiddesired \"amountdesired\" action\n"
            "\nPlace or cancel a trade offer on the distributed token exchange.\n"
            "\nParameters:\n"
            "FromAddress         : the address to send this transaction from\n"
            "PropertyIDForSale   : the property to list for sale\n"
            "AmountForSale       : the amount to list for sale\n"
            "PropertyIDDesired   : the property desired\n"
            "AmountDesired       : the amount desired\n"
            "Action              : the action to take: (1) new, (2) cancel by price, (3) cancel by pair, (4) cancel all\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendtrade_OMNI \"1FromAddress\" PropertyIDForSale \"AmountForSale\" PropertyIDDesired \"AmountDesired\" Action\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    uint32_t propertyIdForSale = ParsePropertyIdUnchecked(params[1]);
    std::string strAmountForSale = params[2].get_str();
    uint32_t  propertyIdDesired = ParsePropertyIdUnchecked(params[3]);
    std::string strAmountDesired = params[4].get_str();
    int64_t action = ParseMetaDexAction(params[5]);

    // setup a few vars
    int64_t amountForSale = 0, amountDesired = 0;
    CMPSPInfo::Entry spForSale;
    CMPSPInfo::Entry spDesired;

    // perform conversions & checks
    if (action != CMPTransaction::CANCEL_EVERYTHING) { // these checks are not applicable to cancel everything
        if (false == _my_sps->getSP(propertyIdForSale, spForSale)) throw JSONRPCError(RPC_INVALID_PARAMETER, "Property for sale does not exist");
        if (false == _my_sps->getSP(propertyIdDesired, spDesired)) throw JSONRPCError(RPC_INVALID_PARAMETER, "Property desired does not exist");
        if (isTestEcosystemProperty(propertyIdForSale) != isTestEcosystemProperty(propertyIdDesired)) throw JSONRPCError(RPC_INVALID_PARAMETER, "Property for sale and property desired must be in the same ecosystem");
        if (propertyIdForSale == propertyIdDesired) throw JSONRPCError(RPC_INVALID_PARAMETER, "Property for sale and property desired must be different");
    }
    if (action <= CMPTransaction::CANCEL_AT_PRICE) { // cancel pair and cancel everything permit zero values
        amountForSale = StrToInt64(strAmountForSale, spForSale.isDivisible());
        amountDesired = StrToInt64(strAmountDesired, spDesired.isDivisible());
        if (0 >= amountForSale) throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for sale");
        if (!isRangeOK(amountForSale)) throw JSONRPCError(RPC_TYPE_ERROR, "Amount for sale not in range");
        if (0 >= amountDesired) throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount desired");
        if (!isRangeOK(amountDesired)) throw JSONRPCError(RPC_TYPE_ERROR, "Amount desired not in range");
    }
    if (action == CMPTransaction::ADD) { // only check for sufficient balance for new trades
        RequireSufficientBalance(fromAddress, propertyIdForSale, amountForSale);
    }

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_MetaDExTrade(propertyIdForSale, amountForSale, propertyIdDesired, amountDesired, action);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, "", "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            PendingAdd(txid, fromAddress, "", MSC_TYPE_METADEX, propertyIdForSale, amountForSale, propertyIdDesired, amountDesired, action);
            return txid.GetHex();
        }
    }
}

// sendchangeissuer_OMNI - Change issuer for a property
Value sendchangeissuer_OMNI(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "sendchangeissuer_OMNI \"fromaddress\" \"toaddress\" propertyid\n"
            "\nCreates and broadcasts a change issuer message for a given currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send this transaction from\n"
            "ToAddress     : the address to transfer administrative control for this property to\n"
            "PropertyID    : the id of the smart property to change issuer\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">omnicored sendchangeissuer_OMNI \"1FromAddress\" \"1ToAddress\" PropertyID\n"
        );

    // obtain parameters & info
    std::string fromAddress = ParseAddress(params[0]);
    std::string toAddress = ParseAddress(params[1]);
    uint32_t propertyId = ParsePropertyId(params[2]);

    // perform checks
    RequireTokenAdministrator(fromAddress, propertyId);

    // create a payload for the transaction
    std::vector<unsigned char> payload = CreatePayload_ChangeIssuer(propertyId);

    // request the wallet build the transaction (and if needed commit it)
    uint256 txid = 0;
    std::string rawHex;
    int result = ClassAgnosticWalletTXBuilder(fromAddress, toAddress, "", 0, payload, txid, rawHex, autoCommit);

    // check error and return the txid (or raw hex depending on autocommit)
    if (result != 0) {
        throw JSONRPCError(result, error_str(result));
    } else {
        if (!autoCommit) {
            return rawHex;
        } else {
            return txid.GetHex();
        }
    }
}


