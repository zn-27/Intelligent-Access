/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

// Include a header file from your module to test.
#include "ns3/mymodule.h"
#include "ns3/intelligent-access-algorithm.h"
#include "ns3/crypto-utils.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/nstime.h"

// An essential include is test.h
#include "ns3/test.h"

// Do not put your test classes in namespace ns3.  You may find it useful
// to use the using directive to access the ns3 namespace directly
using namespace ns3;

// This is an example TestCase.
class MymoduleTestCase1 : public TestCase
{
public:
  MymoduleTestCase1 ();
  virtual ~MymoduleTestCase1 ();

private:
  virtual void DoRun (void);
};

// Add some help text to this case to describe what it is intended to test
MymoduleTestCase1::MymoduleTestCase1 ()
  : TestCase ("Mymodule test case (does nothing)")
{
}

// This destructor does nothing but we include it as a reminder that
// the test case should clean up after itself
MymoduleTestCase1::~MymoduleTestCase1 ()
{
}

//
// This method is the pure virtual method from class TestCase that every
// TestCase must implement
//
void
MymoduleTestCase1::DoRun (void)
{
  // A wide variety of test macros are available in src/core/test.h
  NS_TEST_ASSERT_MSG_EQ (true, true, "true doesn't equal true for some reason");
  // Use this one for floating point comparisons
  NS_TEST_ASSERT_MSG_EQ_TOL (0.01, 0.01, 0.001, "Numbers are not equal within tolerance");
}

// The TestSuite class names the TestSuite, identifies what type of TestSuite,
// and enables the TestCases to be run.  Typically, only the constructor for
// this class must be defined
//
class MymoduleTestSuite : public TestSuite
{
public:
  MymoduleTestSuite ();
};

class IntelligentAccessDecisionTestCase : public TestCase
{
public:
  IntelligentAccessDecisionTestCase()
    : TestCase("multi-attribute access decision and time-to-trigger") {}

private:
  void DoRun() override
  {
    Ptr<IntelligentAccessAlgorithm> algorithm =
        CreateObject<IntelligentAccessAlgorithm>();
    algorithm->SetAttribute("Hysteresis", DoubleValue(0.0));
    algorithm->SetAttribute("TimeToTrigger", TimeValue(Seconds(1.0)));
    algorithm->SetAttribute("ConsecutiveBetter", UintegerValue(2));
    algorithm->SetAttribute("SwitchCostWeight", DoubleValue(0.0));

    IntelligentAccessAlgorithm::Observation current;
    current.key.type = IntelligentAccessAlgorithm::INFRASTRUCTURE;
    current.key.domainId = 1;
    current.key.networkId = "A";
    current.key.gateway = Ipv4Address("10.1.1.1");
    current.key.channelFreqMhz = 2412;
    current.signalDbm = -75.0;
    current.noiseDbm = -95.0;
    current.hopsToGateway = 0;
    current.load = 0.4;
    current.minEnergy = 0.8;
    current.gatewayReachable = true;
    current.addressServiceAvailable = true;
    current.observedAt = Seconds(0);

    IntelligentAccessAlgorithm::Observation target = current;
    target.key.type = IntelligentAccessAlgorithm::ADHOC;
    target.key.domainId = 3;
    target.key.networkId = "Adhoc-C";
    target.key.gateway = Ipv4Address("10.100.3.1");
    target.key.channelFreqMhz = 2462;
    target.signalDbm = -45.0;
    target.load = 0.1;
    target.hopsToGateway = 1;

    algorithm->Update(current);
    algorithm->Update(target);
    auto first = algorithm->Evaluate(&current.key, true, Seconds(0));
    NS_TEST_ASSERT_MSG_EQ(first.shouldSwitch, false,
                          "time-to-trigger must suppress the first decision");

    current.observedAt = Seconds(1.1);
    target.observedAt = Seconds(1.1);
    algorithm->Update(current);
    algorithm->Update(target);
    auto second = algorithm->Evaluate(&current.key, true, Seconds(1.1));
    NS_TEST_ASSERT_MSG_EQ(second.shouldSwitch, true,
                          "stable superior target should pass time-to-trigger");
    NS_TEST_ASSERT_MSG_EQ(second.best.observation.key.domainId, 3u,
                          "algorithm selected the wrong domain");
  }
};

class IntelligentAccessSecurityFilterTestCase : public TestCase
{
public:
  IntelligentAccessSecurityFilterTestCase()
    : TestCase("mandatory security capability filter") {}

private:
  void DoRun() override
  {
    Ptr<IntelligentAccessAlgorithm> algorithm =
        CreateObject<IntelligentAccessAlgorithm>();
    algorithm->SetAttribute(
        "RequiredSecurityCapabilities",
        UintegerValue(IntelligentAccessAlgorithm::SEC_INTEGRITY |
                      IntelligentAccessAlgorithm::SEC_REPLAY_PROTECTION));

    IntelligentAccessAlgorithm::Observation insecure;
    insecure.key.type = IntelligentAccessAlgorithm::ADHOC;
    insecure.key.domainId = 3;
    insecure.key.networkId = "insecure";
    insecure.signalDbm = -35.0;
    insecure.noiseDbm = -95.0;
    insecure.observedAt = Seconds(0);
    algorithm->Update(insecure);

    auto decision = algorithm->Evaluate(nullptr, false, Seconds(0));
    NS_TEST_ASSERT_MSG_EQ(decision.hasCandidate, false,
                          "insecure candidate bypassed mandatory capability filter");
  }
};

class ChebyshevAuthenticatedExchangeTestCase : public TestCase
{
public:
  ChebyshevAuthenticatedExchangeTestCase()
    : TestCase("Chebyshev shared secret and HMAC tamper detection") {}

private:
  void DoRun() override
  {
    const mpz_class modulus = generate_128bit_prime();
    ExtendedChebyshevKeyExchange terminal(modulus);
    ExtendedChebyshevKeyExchange ap(modulus);

    size_t terminalPrivateLength = 0;
    size_t apPrivateLength = 0;
    unsigned char* terminalPrivate =
        terminal.generate_private_key_bytes(&terminalPrivateLength);
    unsigned char* apPrivate =
        ap.generate_private_key_bytes(&apPrivateLength);
    const unsigned char basePoint[] = {2};

    size_t terminalPublicLength = 0;
    size_t apPublicLength = 0;
    unsigned char* terminalPublic =
        terminal.compute_public_key_bytes(
            terminalPrivate, terminalPrivateLength,
            basePoint, sizeof(basePoint), &terminalPublicLength);
    unsigned char* apPublic =
        ap.compute_public_key_bytes(
            apPrivate, apPrivateLength,
            basePoint, sizeof(basePoint), &apPublicLength);

    size_t terminalSecretLength = 0;
    size_t apSecretLength = 0;
    unsigned char* terminalSecret =
        terminal.compute_shared_secret_bytes(
            terminalPrivate, terminalPrivateLength,
            apPublic, apPublicLength, &terminalSecretLength);
    unsigned char* apSecret =
        ap.compute_shared_secret_bytes(
            apPrivate, apPrivateLength,
            terminalPublic, terminalPublicLength, &apSecretLength);

    NS_TEST_ASSERT_MSG_EQ(
        ExtendedChebyshevKeyExchange::bytes_to_hex(
            terminalSecret, terminalSecretLength),
        ExtendedChebyshevKeyExchange::bytes_to_hex(
            apSecret, apSecretLength),
        "terminal and AP derived different shared secrets");
    NS_TEST_ASSERT_MSG_EQ((terminalSecretLength >= 16), true,
                          "derived secret is too short for AP authentication");

    const std::string request =
        "TYPE:IP_REQUEST;MAC:00:00:00:00:00:01;TXID:7";
    const std::string tampered =
        "TYPE:IP_REQUEST;MAC:00:00:00:00:00:01;TXID:8";
    unsigned char* requestHmac =
        CryptoUtils::hmacSha256First64Bits(
            request.data(), request.size(), terminalSecret, 16);
    unsigned char* apHmac =
        CryptoUtils::hmacSha256First64Bits(
            request.data(), request.size(), apSecret, 16);
    unsigned char* tamperedHmac =
        CryptoUtils::hmacSha256First64Bits(
            tampered.data(), tampered.size(), apSecret, 16);
    NS_TEST_ASSERT_MSG_EQ(
        CryptoUtils::bytesToHex(requestHmac, 8),
        CryptoUtils::bytesToHex(apHmac, 8),
        "AP could not verify the terminal request HMAC");
    NS_TEST_ASSERT_MSG_NE(
        CryptoUtils::bytesToHex(requestHmac, 8),
        CryptoUtils::bytesToHex(tamperedHmac, 8),
        "changing TXID did not invalidate the request HMAC");

    CryptoUtils::freeBytes(requestHmac);
    CryptoUtils::freeBytes(apHmac);
    CryptoUtils::freeBytes(tamperedHmac);
    ExtendedChebyshevKeyExchange::free_bytes(terminalPrivate);
    ExtendedChebyshevKeyExchange::free_bytes(apPrivate);
    ExtendedChebyshevKeyExchange::free_bytes(terminalPublic);
    ExtendedChebyshevKeyExchange::free_bytes(apPublic);
    ExtendedChebyshevKeyExchange::free_bytes(terminalSecret);
    ExtendedChebyshevKeyExchange::free_bytes(apSecret);
  }
};

MymoduleTestSuite::MymoduleTestSuite ()
  : TestSuite ("mymodule", UNIT)
{
  // TestDuration for TestCase can be QUICK, EXTENSIVE or TAKES_FOREVER
  AddTestCase (new MymoduleTestCase1, TestCase::QUICK);
  AddTestCase(new IntelligentAccessDecisionTestCase, TestCase::QUICK);
  AddTestCase(new IntelligentAccessSecurityFilterTestCase, TestCase::QUICK);
  AddTestCase(new ChebyshevAuthenticatedExchangeTestCase, TestCase::QUICK);
}

// Do not forget to allocate an instance of this TestSuite
static MymoduleTestSuite smymoduleTestSuite;
