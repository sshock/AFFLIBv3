#!/bin/sh
# 
# test the signing tools

# This file is a work of a US government employee and as such is in the Public domain.
# Simson L. Garfinkel, March 12, 2012

unset AFFLIB_PASSPHRASE

BASE=`mktemp -t baseXXXXX`
AGENT_PEM=$BASE.agent.pem
ANALYST_PEM=$BASE.analyst.pem
ARCHIVES_PEM=$BASE.archives.pem
EVIDENCE=$BASE.evidence.aff
EVIDENCE2=$BASE.evidence2.aff
EVIDENCE3=$BASE.evidence3.aff

/bin/rm -f $AGENT_PEM $ANALYST_PEM $ARCHIVES_PEM $EVIDENCE $EVIDENCE2 $EVIDENCE3

echo TEST $0
echo === MAKING THE TEST FILES ===

export PATH=$srcdir:../tools:../../tools:.:$PATH
test_make_random_iso.sh rawevidence.raw

if [ ! -r rawevidence.raw ]; then
  echo rawevidence.raw not made
  exit 1
fi

echo ==== AFSIGN TEST ===
echo Making X.509 keys

openssl req -x509 -newkey rsa:1024 -keyout $AGENT_PEM -out $AGENT_PEM -nodes -subj "/C=US/ST=California/L=Remote/O=Country Govt./OU=Sherif Dept/CN=Mr. Agent/emailAddress=agent@investiations.com"
openssl req -x509 -newkey rsa:1024 -keyout $ANALYST_PEM -out $ANALYST_PEM -nodes -subj "/C=US/ST=California/L=Remote/O=State Police/OU=Forensics/CN=Ms. Analyst/emailAddress=analyst@investiations.com"
openssl req -x509 -newkey rsa:1024 -keyout $ARCHIVES_PEM -out $ARCHIVES_PEM -nodes -subj "/C=US/ST=CA/L=Remote/O=Archives/OU=Electronic/CN=Dr. Librarian/emailAddress=drbits@investiations.com"

echo Making an AFF file to sign
ls -l rawevidence.raw
affconvert -o $EVIDENCE rawevidence.raw 
echo Initial AFF file made:
ls -l $EVIDENCE
if ! affinfo -a $EVIDENCE ; then exit 1 ; fi

echo Signing AFF file...
echo affsign -k $AGENT_PEM $EVIDENCE 
if ! affsign -k $AGENT_PEM $EVIDENCE ; then echo affsign failed ; exit 1 ; fi 

echo Verifying Signature...
echo affverify $EVIDENCE 
if ! affverify $EVIDENCE ; then echo affverify failed ; exit 1 ; fi ; 

echo Signature test 1 passed
echo Testing chain-of-custody signatures

echo Step 10: Copying original raw file to evidence1.aff
if ! affcopy -k $AGENT_PEM rawevidence.raw evidence1.aff ; then exit 1; fi
echo Step 11: Running affinfo on evidence1.aff
if ! affinfo -a evidence1.aff ; then exit 1 ; fi
echo Step 12: Comparing rawevidence.raw to evidence1.aff
if ! affcompare rawevidence.raw evidence1.aff ; then exit 1 ; fi
echo Step 13: Verifying evidence1
if ! affverify evidence1.aff ; then exit 1 ; fi

echo
echo Making the second generation copy
echo "This copy was made by the analyst" | affcopy -z -k $ANALYST_PEM -n evidence1.aff $EVIDENCE2
if ! affinfo -a $EVIDENCE2 ; then exit 1 ; fi
if ! affcompare rawevidence.raw $EVIDENCE2 ; then exit 1 ; fi
if ! affverify $EVIDENCE2 ; then exit 1 ; fi
echo
echo Making the third generation copy
echo "This copy was made by the archives" | affcopy -z -k $ARCHIVES_PEM -n $EVIDENCE2 $EVIDENCE3
if ! affinfo -a $EVIDENCE3 ; then exit 1 ; fi
if ! affcompare rawevidence.raw $EVIDENCE3 ; then exit 1 ; fi
if ! affverify $EVIDENCE3 ; then exit 1 ; fi


echo All tests passed successfully
echo Erasing temporary files.
rm -f $AGENT_PEM $ARCHIVES_PEM $ANALYST_PEM $EVIDENCE evidence.afm rawevidence.raw cevidence.raw $EVIDENCE2 $EVIDENCE3 $EVIDENCE
exit 0

