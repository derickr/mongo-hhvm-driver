--TEST--
MongoDB\BSON\Decimal128 round trip
--SKIPIF--
<?php if ( !class_exists( "MongoDB\BSON\Decimal128" ) ) { echo "skip MongoDB\BSON\Decimal128 class is not available\n"; } ?>
--FILE--
<?php
include dirname(__FILE__) . '/utils.inc';

$m = new MongoDB\Driver\Manager("mongodb://localhost:27017");
cleanup( $m );

$d = new \MongoDB\BSON\Decimal128("1234.5678");

$bw = new MongoDB\Driver\BulkWrite( [ 'ordered' => false ] );
$_id = $bw->insert( [ 'decimal' => $d ] );
$r = $m->executeBulkWrite( 'demo.test', $bw );

$query = new MongoDB\Driver\Query( [] );
$cursor = $m->executeQuery( "demo.test", $query );
var_dump( $cursor->toArray() );
?>
--EXPECTF--
array(1) {
  [0]=>
  object(stdClass)#%d (%d) {
    ["_id"]=>
    object(MongoDB\BSON\ObjectID)#%d (1) {
      ["oid"]=>
      string(24) "%s"
    }
    ["decimal"]=>
    object(MongoDB\BSON\Decimal128)#%d (1) {
      ["decimal"]=>
      string(9) "1234.5678"
    }
  }
}
