Benchmarks
==========

The tests (tox) run the benchmark code with very small files. The benchmarks is about running the same
code with Read Shards that have the expected size in production (100GB) and verify:

* The time required to build a Read Shard is less than 5 times the
  time to copy a file of the same size.  It guards against regressions
  that would have a significant performance impact.

* The time required to build and write the perfect hash table is
  always a fraction of the time required to copy the content of the
  objects.

* It is possible to perform at least 10,000 object lookups per second.


Build performances
------------------

In both cases the distribution of the object sizes is uniform.

With a small number of large (<100MB) objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The goal is to verify that manipulating up to the largest supported
object size actually works and does not trigger problems in the
extreme case that all objects have the maximum size.

* time tox run -e py3 -- --basetemp=/mnt/pytest -s --shard-path /mnt/payload --shard-size $((100 * 1024)) --object-max-size $((100 * 1024 * 1024)) -k test_build_speed
  number of objects = 2057, total size = 107374116576
  baseline 165.85, write_duration 327.19, build_duration 0.0062, total_duration 327.19

With a large number of small (<4KB) objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It approximates the maximum number of objects a Read Shard can
contain. The goal is to verify that creating a perfect hash with this
many items does not require more than a fraction of the time required
to copy the objects.

* time tox run -e py3 -- --basetemp=/mnt/pytest -s --shard-path /mnt/payload --shard-size $((100 * 1024)) --object-max-size $((4 * 1024)) -k test_build_speed
  number of objects = 45973694, total size = 105903024192
  baseline 165.74, write_duration 495.07, build_duration 24.21, total_duration 519.28


Object lookup performances
--------------------------

The machine has 200GB of RAM and can therefore approximately cache the
content of two Read Shards which can have a significant impact on
performances. To minimize that effect, four Read Shard are created
totaling 400GB. All objects are looked up in all shards to verify
the lookup speed is greater than 5,000 objects per second.

* time tox run -e py3 -- --basetemp=/mnt/pytest -s -k test_lookup_speed --lookups $((100 * 1024 * 1024)) --shard-size $((100 * 1024)) --object-max-size $((4 * 1024)) swh/perfecthash/tests/test_hash.py  --shard-path /mnt/payload --shard-count 4
  number of objects = 45974390, total size = 105903001920
  key lookups speed = 9769.68/s


Setup via Fed4Fire
------------------

* https://www.fed4fire.eu/
* /opt/jFed/jFed-Experimenter
* Create an experiment with one machine
* Click on Topology Viewer and run the experiment (name test)
* Once the provisionning is complete (Testing connectivity to resources on Grid5000) click Export As
* Choose Export Configuration Management Settings
* Save under test.zip and unzip
* ssh -i id_rsa -F ssh-config node0
* alias sudo=sudo-g5k

Setup via Grid5000
------------------

* https://www.grid5000.fr/
* oarsub -I -l "{cluster='dahu'}/host=1,walltime=1" -t deploy
* kadeploy3 -f $OAR_NODE_FILE -e debian11-x64-base -k
* ssh root@$(tail -1 $OAR_NODE_FILE)

Common setup
------------

* mkfs.ext4 /dev/sdc
* mount /dev/sdc /mnt
* apt-get install -y python3-venv python3-dev libcmph-dev gcc git emacs-nox
* git clone -b wip-benchmark https://git.easter-eggs.org/biceps/swh-perfecthash/
* python3 -m venv bench
* source bench/bin/activate
* cd swh-perfecthash
* pip install -r requirements.txt -r requirements-test.txt tox wheel
* tox run -e py3
